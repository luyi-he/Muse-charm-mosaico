"""CoreS3 USB voice bridge: local Whisper -> Codex app-server -> CoreS3 audio.

No Muse service. Codex uses the existing CLI login. Qwen is optional; its key
is kept in RAM and never sent to Codex or written to the repository.
"""
from __future__ import annotations
import argparse
import asyncio
import base64
import collections
import json
import os
from pathlib import Path
import queue
import secrets
import shutil
import subprocess
import tempfile
import threading
import time
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import numpy as np
import serial


def codex_binary():
    # Prefer the installed native CLI; do not spawn a shell with voice text.
    found=shutil.which("codex.exe")
    if found:return found
    root=Path(os.environ.get("APPDATA", ""))/"npm/node_modules/@openai/codex/node_modules"
    found=list(root.glob("@openai/codex-win32-*/vendor/*/bin/codex.exe"))
    if found:return str(found[0])
    raise RuntimeError("Install Codex CLI and run codex login first")


class Codex:
    def __init__(self,cwd):
        self.proc=subprocess.Popen([codex_binary(),"app-server","--listen","stdio://"],
            stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,
            text=True,encoding="utf-8",bufsize=1,
            creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
        self.pending={};self.events=queue.Queue();self.counter=0;self.lock=threading.Lock()
        self.thread=None;self.turn=None
        threading.Thread(target=self.read,daemon=True).start()
        self.rpc("initialize",{"clientInfo":{"name":"cores3_usb","title":"CoreS3 USB companion","version":"2.0"}})
        self.send({"method":"initialized","params":{}})
        result=self.rpc("thread/start",{"cwd":str(cwd),"approvalPolicy":"never","sandbox":"read-only",
            "baseInstructions":"你是 CoreS3 上的语音伙伴，回答简短、自然的中文，通常不超过120字。用户输入由本地语音识别产生，可能有错字。不要调用工具、读文件或执行命令；只进行对话。"})
        self.thread=result["thread"]["id"]

    def send(self,obj):
        with self.lock:
            self.proc.stdin.write(json.dumps(obj,ensure_ascii=False)+"\n");self.proc.stdin.flush()

    def read(self):
        for line in self.proc.stdout:
            try:msg=json.loads(line)
            except ValueError:continue
            if "id" in msg and "method" not in msg:
                q=self.pending.get(msg["id"])
                if q:q.put(msg)
            elif "id" in msg:
                # Voice chat grants no new file/command/MCP permissions.
                self.send({"id":msg["id"],"error":{"code":-32601,"message":"Tools and approvals unavailable in voice chat"}})
            else:self.events.put(msg)
        for q in list(self.pending.values()):q.put({"error":{"message":"Codex process stopped"}})

    def rpc(self,method,params,timeout=60):
        self.counter+=1;ident=self.counter;q=queue.Queue();self.pending[ident]=q
        try:
            self.send({"id":ident,"method":method,"params":params});msg=q.get(timeout=timeout)
            if "error" in msg:raise RuntimeError(msg["error"].get("message","Codex request failed"))
            return msg["result"]
        finally:self.pending.pop(ident,None)

    def ask(self,text,current):
        while not self.events.empty():self.events.get_nowait()
        result=self.rpc("turn/start",{"threadId":self.thread,"input":[{"type":"text","text":text}]})
        self.turn=result["turn"]["id"];parts=[];final=[];deadline=time.monotonic()+180
        try:
            while time.monotonic()<deadline:
                if not current():
                    self.rpc("turn/interrupt",{"threadId":self.thread,"turnId":self.turn});return ""
                try:msg=self.events.get(timeout=.25)
                except queue.Empty:continue
                params=msg.get("params",{});method=msg.get("method","")
                if params.get("threadId") not in (None,self.thread):continue
                if method=="item/agentMessage/delta":parts.append(params.get("delta",""))
                elif method=="item/completed":
                    item=params.get("item",{})
                    if item.get("type")=="agentMessage":final.append(item.get("text",""))
                elif method=="turn/completed":
                    turn=params.get("turn",{})
                    if turn.get("status")=="failed":raise RuntimeError("Codex turn failed")
                    answer="\n".join(final) or "".join(parts)
                    if not answer:raise RuntimeError("Codex returned no text")
                    return answer
            self.rpc("turn/interrupt",{"threadId":self.thread,"turnId":self.turn})
            raise TimeoutError("Codex response timeout")
        finally:self.turn=None

    def close(self):
        self.proc.terminate()


def windows_tts(text):
    with tempfile.TemporaryDirectory(prefix="cores3-voice-") as temp:
        source=Path(temp)/"text.txt";target=Path(temp)/"voice.wav"
        source.write_text(text,encoding="utf-8")
        subprocess.run(["powershell.exe","-NoProfile","-File",str(Path(__file__).with_name("speak.ps1")),
            "-TextPath",str(source),"-WavePath",str(target)],check=True,timeout=90,
            stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,
            creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
        with wave.open(str(target),"rb") as f:
            if (f.getnchannels(),f.getsampwidth(),f.getframerate())!=(1,2,16000):raise ValueError("Wrong TTS audio format")
            return f.readframes(f.getnframes())


async def qwen_tts(text,key,on_audio=None,current=lambda:True):
    import websockets
    url="wss://dashscope.aliyuncs.com/api-ws/v1/realtime?model=qwen3-tts-flash-realtime"
    async with websockets.connect(url,additional_headers={"Authorization":"Bearer "+key},max_size=4*1024*1024) as ws:
        async def send(kind,**data):
            await ws.send(json.dumps({"type":kind,"event_id":secrets.token_hex(8),**data}))
        await send("session.update",session={"voice":"Cherry","mode":"commit","response_format":"pcm","sample_rate":16000,"language_type":"Chinese"})
        audio=bytearray()
        async with asyncio.timeout(120):
            async for raw in ws:
                if not current():return b""
                event=json.loads(raw);kind=event.get("type")
                if kind=="error":raise RuntimeError("Qwen TTS rejected the request; check key, region and quota")
                if kind=="session.updated":
                    await send("input_text_buffer.append",text=text);await send("input_text_buffer.commit")
                elif kind=="response.audio.delta":
                    chunk=base64.b64decode(event["delta"])
                    if on_audio:await asyncio.to_thread(on_audio,chunk)
                    else:audio.extend(chunk)
                elif kind=="response.done":
                    if event.get("response",{}).get("status")!="completed" or (not on_audio and not audio):raise RuntimeError("Qwen TTS incomplete")
                    return bytes(audio)
        raise RuntimeError("Qwen TTS closed before completion")


class Bridge:
    def __init__(self,port,model,cwd):
        self.status="正在启动";self.logs=collections.deque(maxlen=40);self.last_text="";self.reply=""
        self.key=os.environ.get("DASHSCOPE_API_KEY","");self.web_nonce=secrets.token_urlsafe(24)
        self.gen=0;self.muted=False;self.pcm=bytearray();self.jobs=queue.Queue(maxsize=2);self.pending_text=None
        self.acks=queue.Queue();self.tx=threading.Lock();self.alive=True;self.ready=False;self.last_reply=""
        self.serial=serial.Serial(baudrate=115200,timeout=.25,write_timeout=5)
        self.serial.dtr=False;self.serial.rts=False;self.serial.port=port;self.serial.open()
        threading.Thread(target=self.read,daemon=True).start()
        self.codex=Codex(cwd)
        self.status="正在加载本地语音识别"
        from faster_whisper import WhisperModel
        self.whisper=WhisperModel(model,device="cpu",compute_type="int8",local_files_only=True)
        threading.Thread(target=self.worker,daemon=True).start()
        self.ready=True;self.status="USB Codex 已就绪"
        threading.Thread(target=self.heartbeat,daemon=True).start()

    def send(self,type,**fields):
        obj={"type":type,"generation":self.gen,**fields}
        with self.tx:self.serial.write((json.dumps(obj,ensure_ascii=False)+"\n").encode())

    def heartbeat(self):
        while self.alive:
            try:self.send("hello")
            except (serial.SerialException,OSError):self.status="USB 已断开";self.alive=False;break
            time.sleep(3)

    def enqueue(self,job):
        try:self.jobs.put_nowait(job)
        except queue.Full:self.status="操作繁忙，请稍后再试"

    def read(self):
        buffered=bytearray()
        while self.alive:
            try:chunk=self.serial.read_until(b"\n")
            except (serial.SerialException,OSError):self.status="USB 已断开";self.alive=False;break
            if not chunk:continue
            buffered.extend(chunk)
            if len(buffered)>8192:buffered.clear();continue
            if not buffered.endswith(b"\n"):continue
            line=bytes(buffered);buffered.clear()
            if not line.startswith(b"@cores3 "):
                message=line.decode("utf-8",errors="replace").strip()
                if message:self.logs.append(message)
                continue
            try:obj=json.loads(line[8:])
            except ValueError:continue
            kind=obj.get("type");gen=obj.get("generation",0)
            if kind=="hello":
                self.muted=obj.get("muted",False);self.battery=obj.get("battery",-1)
            elif kind=="cancel":self.gen=gen;self.pcm=bytearray()
            elif kind=="recording":self.gen=gen;self.pcm=bytearray();self.status="接收录音"
            elif kind=="pcm" and gen==self.gen:
                if len(self.pcm)<640000:self.pcm.extend(base64.b64decode(obj["data"]))
            elif kind=="recording_end" and gen==self.gen:self.enqueue((gen,bytes(self.pcm),False))
            elif kind=="text_ready" and self.pending_text is not None:
                self.gen=gen;self.enqueue((gen,self.pending_text,False));self.pending_text=None
            elif kind=="audio_ack":self.acks.put(obj)
            elif kind=="retry":self.gen=gen;self.enqueue((gen,self.last_reply,True))

    def playback(self,pcm,gen,start_seq=0):
        next_seq=start_seq
        for seq,offset in enumerate(range(0,len(pcm),2048),start_seq):
            if gen!=self.gen:return next_seq
            self.send("audio",generation=gen,seq=seq,data=base64.b64encode(pcm[offset:offset+2048]).decode())
            deadline=time.monotonic()+5
            while time.monotonic()<deadline:
                try:ack=self.acks.get(timeout=.25)
                except queue.Empty:continue
                if ack.get("generation")==gen and ack.get("seq")==seq:
                    if ack.get("result")!=0:raise RuntimeError("CoreS3 speaker write failed")
                    break
            else:raise TimeoutError("CoreS3 audio acknowledgement timeout")
            next_seq=seq+1
        return next_seq

    def worker(self):
        while self.alive:
            gen,data,retry=self.jobs.get()
            if gen!=self.gen:continue
            try:
                started=time.monotonic()
                if retry:answer=data
                else:
                    if isinstance(data,bytes):
                        self.status="本地识别中"
                        audio=np.frombuffer(data,dtype="<i2").astype(np.float32)/32768
                        segments,_=self.whisper.transcribe(audio,language="zh",beam_size=3,vad_filter=True)
                        text="".join(s.text for s in segments).strip()
                    else:text=data
                    if not text:raise ValueError("没有识别到说话，请再试一次")
                    self.last_text=text;self.status="Codex 正在回答"
                    self.logs.append(f"识别耗时 {time.monotonic()-started:.1f}s")
                    started=time.monotonic()
                    answer=self.codex.ask(text,lambda:gen==self.gen)
                    self.logs.append(f"Codex 耗时 {time.monotonic()-started:.1f}s")
                if gen!=self.gen:continue
                self.reply=answer;self.last_reply=answer
                self.send("reply",generation=gen,text=answer[:1300],speaking=not self.muted)
                if not self.muted:
                    self.status="朗读中"
                    started=time.monotonic()
                    if self.key:
                        sequence=0;first=True;tail=b""
                        def stream(chunk):
                            nonlocal sequence,first,tail
                            if first:
                                self.logs.append(f"Qwen 首段音频 {time.monotonic()-started:.1f}s")
                                first=False
                            chunk=tail+chunk;even=len(chunk)&~1;tail=chunk[even:]
                            sequence=self.playback(chunk[:even],gen,sequence)
                        asyncio.run(qwen_tts(answer,self.key,stream,lambda:gen==self.gen))
                        if first:raise RuntimeError("Qwen returned no audio")
                    else:self.playback(windows_tts(answer),gen)
                if gen==self.gen:self.send("done",generation=gen);self.status="USB Codex 已就绪"
            except Exception as exc:
                self.status=str(exc);self.logs.append(str(exc))
                if gen==self.gen:self.send("error",generation=gen,text="对话或朗读失败，请查看电脑桥接页面")

    def ask_text(self,text):
        # Exercise the same on-device press/release generation as touch input.
        if self.pending_text is not None:return
        self.pending_text=text
        self.send("key",pressed=True,text_mode=True);time.sleep(.25);self.send("key",pressed=False)


PAGE='''<!doctype html><meta charset="utf-8"><title>CoreS3 · Codex</title>
<style>body{max-width:720px;margin:40px auto;font:16px system-ui;padding:20px;background:#faf7f2}input{width:85%;padding:10px}button{padding:10px;margin:8px}pre{white-space:pre-wrap;background:white;padding:16px}</style>
<h1>CoreS3 · 本机 Codex</h1><p>用 USB 连接设备，按住屏幕底部「按住说话」，松开发送。</p>
<p id="status"></p><p id="battery"></p><h3>文字测试</h3><input id="text" placeholder="也可以在这里输入问题"><button onclick="ask()">发送</button>
<h3>听到的问题</h3><pre id="heard"></pre><h3>Codex 回答</h3><pre id="reply"></pre>
<h3>朗读设置</h3><p>默认使用电脑的中文朗读。填写百炼密钥后改用 Qwen Cherry；密钥仅保存在本次程序内存中。</p>
<input type="password" id="key" placeholder="可选：百炼 API Key"><button onclick="tts()">使用 Qwen</button><button onclick="local()">使用电脑朗读</button>
<details><summary>设备诊断日志</summary><pre id="logs"></pre></details>
<script>const nonce='NONCE';async function post(path,data){const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/json','X-Bridge-Nonce':nonce},body:JSON.stringify(data)});if(!r.ok)alert('操作失败，请重试');}
function ask(){post('/ask',{text:document.querySelector('#text').value})}function tts(){post('/tts',{key:document.querySelector('#key').value});document.querySelector('#key').value=''}function local(){post('/tts',{key:''})}
async function refresh(){const s=await(await fetch('/status')).json();for(const [id,k] of [['status','status'],['heard','heard'],['reply','reply'],['logs','logs']])document.getElementById(id).textContent=s[k];document.getElementById('battery').textContent='设备电量：'+(s.battery<0?'暂无有效读数':s.battery+'%')+' · 朗读：'+s.tts}setInterval(refresh,1000);refresh();</script>'''


def serve(bridge,port):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*args):pass
        def respond(self,status,body,content_type="application/json"):
            self.send_response(status);self.send_header("Content-Type",content_type+"; charset=utf-8")
            self.send_header("Cache-Control","no-store");self.end_headers();self.wfile.write(body)
        def do_GET(self):
            if self.headers.get("Host") not in (f"127.0.0.1:{port}",f"localhost:{port}"):return self.respond(403,b"{}")
            if self.path=="/":return self.respond(200,PAGE.replace("NONCE",bridge.web_nonce).encode(),"text/html")
            if self.path=="/status":return self.respond(200,json.dumps({"status":bridge.status,"heard":bridge.last_text,
                "reply":bridge.reply,"battery":getattr(bridge,"battery",-1),"tts":"Qwen" if bridge.key else "电脑中文朗读",
                "logs":"\n".join(bridge.logs)},ensure_ascii=False).encode())
            self.respond(404,b"{}")
        def do_POST(self):
            if self.headers.get("Host") not in (f"127.0.0.1:{port}",f"localhost:{port}"):return self.respond(403,b"{}")
            if self.headers.get("X-Bridge-Nonce")!=bridge.web_nonce:return self.respond(403,b"{}")
            try:
                length=int(self.headers.get("Content-Length",0))
                if not 0<length<=8192:raise ValueError()
                data=json.loads(self.rfile.read(length))
                if self.path=="/tts":bridge.key=data.get("key","").strip()
                elif self.path=="/ask":
                    text=data.get("text","").strip()
                    if not text or len(text)>2000:raise ValueError()
                    threading.Thread(target=bridge.ask_text,args=(text,),daemon=True).start()
                else:return self.respond(404,b"{}")
                self.respond(200,b"{}")
            except (ValueError,TypeError):self.respond(400,b"{}")
    ThreadingHTTPServer(("127.0.0.1",port),Handler).serve_forever()


def main():
    parser=argparse.ArgumentParser();parser.add_argument("--port",default="COM4")
    parser.add_argument("--model",default="base");parser.add_argument("--web-port",type=int,default=8765)
    args=parser.parse_args()
    # Keep the voice thread independent of source files and this coding chat.
    cwd=Path(os.environ.get("LOCALAPPDATA",str(Path.home())))/"CoreS3Codex/voice"
    cwd.mkdir(parents=True,exist_ok=True)
    bridge=Bridge(args.port,args.model,cwd)
    print(f"Ready: http://127.0.0.1:{args.web_port}",flush=True)
    try:serve(bridge,args.web_port)
    finally:bridge.alive=False;bridge.serial.close();bridge.codex.close()

if __name__=="__main__":main()
