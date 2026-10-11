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
from urllib.parse import urlparse, parse_qs
import numpy as np
import serial


from codex_client import Codex, ThreadOwnedError
from audio_api import transcribe as openai_stt, speak as openai_tts
from gemini_audio import transcribe as gemini_stt, speak as gemini_tts


def windows_voices():
    result=subprocess.run(["powershell.exe","-NoProfile","-File",str(Path(__file__).with_name("speak.ps1")),"-ListVoices"],check=True,capture_output=True,text=True,timeout=15,creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
    return json.loads(result.stdout)


def windows_tts(text,voice="Microsoft Huihui"):
    with tempfile.TemporaryDirectory(prefix="cores3-voice-") as temp:
        source=Path(temp)/"text.txt";target=Path(temp)/"voice.wav"
        source.write_text(text,encoding="utf-8")
        subprocess.run(["powershell.exe","-NoProfile","-File",str(Path(__file__).with_name("speak.ps1")),
            "-TextPath",str(source),"-WavePath",str(target),"-Voice",voice],check=True,timeout=90,
            stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,
            creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
        import av
        result=[];resampler=av.AudioResampler(format='s16',layout='mono',rate=16000)
        with av.open(str(target)) as container:
            for frame in container.decode(audio=0):
                for chunk in resampler.resample(frame):result.append(bytes(chunk.planes[0])[:chunk.samples*2])
            for chunk in resampler.resample(None):result.append(bytes(chunk.planes[0])[:chunk.samples*2])
        return b''.join(result)



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
        self.openai_key=os.environ.get('OPENAI_API_KEY','');self.voice='marin'
        self.tts_mode='openai' if self.openai_key else 'qwen' if self.key else 'windows'
        self.stt_mode='openai' if self.openai_key else 'local'
        self.gemini_key=os.environ.get('GEMINI_API_KEY','');self.gemini_voice='Aoede'
        if self.gemini_key:self.tts_mode='gemini';self.stt_mode='gemini'
        self.windows_voices=windows_voices();self.windows_voice='Microsoft Huihui' if 'Microsoft Huihui' in self.windows_voices else next(iter(self.windows_voices),'')
        self.chat_title='CoreS3 语音助手'
        self.gen=0;self.muted=False;self.pcm=bytearray();self.jobs=queue.Queue(maxsize=2);self.pending_text=None
        self.acks=queue.Queue();self.tx=threading.Lock();self.alive=True;self.ready=False;self.last_reply=""
        self.serial=serial.Serial(baudrate=115200,timeout=.25,write_timeout=5)
        self.serial.dtr=False;self.serial.rts=False;self.serial.port=port;self.serial.open();self.connected=True
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
        with self.tx:
            if not self.connected:raise serial.SerialException('USB disconnected')
            self.serial.write((json.dumps(obj,ensure_ascii=False)+"\n").encode())

    def disconnected(self):
        if getattr(self,'connected',True):
            self.connected=False;self.gen+=1;self.pcm=bytearray();self.pending_text=None
            self.logs.append('USB 连接中断，等待自动重连')
        self.status='USB 已断开，正在自动重连'

    def reconnect(self):
        with self.tx:
            try:
                if self.serial.is_open:self.serial.close()
                self.serial.open()
            except (serial.SerialException,OSError):return False
            self.connected=True;self.pcm=bytearray()
        self.logs.append('USB 串口已重新打开，等待设备心跳')
        self.status='等待设备心跳'
        return True

    def send_error(self,gen,text):
        try:self.send('error',generation=gen,text=text)
        except (serial.SerialException,OSError):self.disconnected()

    def heartbeat(self):
        while self.alive:
            try:self.send("hello")
            except (serial.SerialException,OSError):self.disconnected()
            time.sleep(3)

    def enqueue(self,job):
        try:self.jobs.put_nowait(job)
        except queue.Full:self.status="操作繁忙，请稍后再试"

    def read(self):
        buffered=bytearray()
        while self.alive:
            if not getattr(self,'connected',True):
                if not self.reconnect():time.sleep(1);continue
            try:chunk=self.serial.read_until(b"\n")
            except (serial.SerialException,OSError):
                buffered.clear();self.disconnected();continue
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
                if self.status=='等待设备心跳':self.status='USB Codex 已就绪';self.logs.append('设备心跳已恢复')
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
                        self.status="语音识别中"
                        if self.stt_mode=='gemini':
                            if not self.gemini_key:raise ValueError('请先填写 Gemini API Key')
                            text=gemini_stt(data,self.gemini_key)
                        elif self.stt_mode=='openai':
                            if not self.openai_key:raise ValueError('请先填写 OpenAI API Key')
                            text=openai_stt(data,self.openai_key)
                        else:
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
                    if self.tts_mode in ('qwen','openai','gemini'):
                        sequence=0;first=True;tail=b""
                        def stream(chunk):
                            nonlocal sequence,first,tail
                            if first:
                                self.logs.append(f"{self.tts_mode} 首段音频 {time.monotonic()-started:.1f}s")
                                first=False
                            chunk=tail+chunk;even=len(chunk)&~1;tail=chunk[even:]
                            sequence=self.playback(chunk[:even],gen,sequence)
                        if self.tts_mode=='gemini':
                            if not self.gemini_key:raise ValueError('请先填写 Gemini API Key')
                            gemini_tts(answer,self.gemini_key,self.gemini_voice,stream,lambda:gen==self.gen)
                        elif self.tts_mode=='openai':
                            if not self.openai_key:raise ValueError('请先填写 OpenAI API Key')
                            openai_tts(answer,self.openai_key,self.voice,stream,lambda:gen==self.gen)
                        else:
                            if not self.key:raise ValueError('请先填写百炼 API Key')
                            asyncio.run(qwen_tts(answer,self.key,stream,lambda:gen==self.gen))
                        if first and gen==self.gen:raise RuntimeError("语音服务没有返回音频")
                    else:self.playback(windows_tts(answer,self.windows_voice),gen)
                if gen==self.gen:self.send("done",generation=gen);self.status="USB Codex 已就绪"
            except (serial.SerialException,OSError):
                self.disconnected()
            except ThreadOwnedError as exc:
                message=str(exc);self.status=message;self.reply=message;self.last_reply=message
                self.logs.append(message)
                if gen==self.gen:
                    self.send('reply',generation=gen,text=message,speaking=not self.muted)
                    try:
                        if not self.muted:self.playback(windows_tts(message,self.windows_voice),gen)
                        if gen==self.gen:self.send('done',generation=gen)
                    except Exception:
                        if gen==self.gen:self.send_error(gen,message)
            except Exception as exc:
                self.status=str(exc);self.logs.append(str(exc))
                if gen==self.gen:self.send_error(gen,"对话或朗读失败，请查看电脑桥接页面")

    def ask_text(self,text):
        # Exercise the same on-device press/release generation as touch input.
        if self.pending_text is not None:return
        self.pending_text=text
        try:self.send("key",pressed=True,text_mode=True);time.sleep(.25);self.send("key",pressed=False)
        except (serial.SerialException,OSError):self.disconnected()


PAGE=Path(__file__).with_name('page.html').read_text(encoding='utf-8')


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
                "reply":bridge.reply,"battery":getattr(bridge,"battery",-1),"tts":bridge.tts_mode,
                "stt":bridge.stt_mode,"windows_voice":bridge.windows_voice,"windows_voices":bridge.windows_voices,"selected":bridge.codex.selected,"chat_title":bridge.codex.selected_title,
                "logs":"\n".join(bridge.logs)},ensure_ascii=False).encode())
            try:
                if self.path=='/chats':return self.respond(200,json.dumps(bridge.codex.list_chats(),ensure_ascii=False).encode())
                if self.path.startswith('/history?'):
                    ident=parse_qs(urlparse(self.path).query)['id'][0]
                    return self.respond(200,json.dumps(bridge.codex.history(ident),ensure_ascii=False).encode())
            except Exception:return self.respond(400,b'{"error":"Cannot read chat"}')
            self.respond(404,b"{}")
        def do_POST(self):
            if self.headers.get("Host") not in (f"127.0.0.1:{port}",f"localhost:{port}"):return self.respond(403,b"{}")
            if self.headers.get("X-Bridge-Nonce")!=bridge.web_nonce:return self.respond(403,b"{}")
            try:
                length=int(self.headers.get("Content-Length",0))
                if not 0<length<=8192:raise ValueError()
                data=json.loads(self.rfile.read(length))
                if self.path=="/tts":
                    bridge.key=data.get("key","").strip();bridge.tts_mode='qwen' if bridge.key else 'windows'
                elif self.path=='/audio':
                    tts=data.get('tts',bridge.tts_mode);stt=data.get('stt',bridge.stt_mode);voice=data.get('voice',bridge.voice)
                    if tts not in ('openai','qwen','windows','gemini') or stt not in ('openai','local','gemini') or voice not in ('coral','marin','cedar','alloy','shimmer'):raise ValueError()
                    windows_voice=data.get('windows_voice',bridge.windows_voice)
                    if windows_voice not in bridge.windows_voices:raise ValueError('电脑未安装这个声音')
                    gemini_voice=data.get('gemini_voice',bridge.gemini_voice)
                    if gemini_voice not in ('Aoede','Kore','Puck','Leda','Zephyr'):raise ValueError()
                    if data.get('openai_key'):bridge.openai_key=data['openai_key'].strip()
                    if data.get('qwen_key'):bridge.key=data['qwen_key'].strip()
                    if data.get('gemini_key'):bridge.gemini_key=data['gemini_key'].strip()
                    if (tts=='gemini' or stt=='gemini') and not bridge.gemini_key:raise ValueError('请先填写 Gemini API Key')
                    if (tts=='openai' or stt=='openai') and not bridge.openai_key:raise ValueError('请先填写 OpenAI API Key')
                    if tts=='qwen' and not bridge.key:raise ValueError('请先填写百炼 API Key')
                    bridge.tts_mode=tts;bridge.stt_mode=stt;bridge.voice=voice;bridge.gemini_voice=gemini_voice;bridge.windows_voice=windows_voice
                elif self.path in ('/select','/create'):
                    if not bridge.codex.operation.acquire(blocking=False):return self.respond(409,b'{"error":"Codex is responding; try again shortly"}')
                    try:
                        if self.path=='/select':
                            ident=data.get('id');bridge.codex.select(ident)
                            bridge.chat_title=bridge.codex.history(ident)['title'] if ident else 'CoreS3 语音助手'
                        else:
                            title=data.get('title','').strip()
                            if not title:raise ValueError('请填写聊天名称')
                            created=bridge.codex.create(title,data.get('cwd') or None);bridge.chat_title=created['title']
                            bridge.codex.release_writer()
                    finally:bridge.codex.operation.release()
                    if self.path=='/create' and data.get('prompt'):
                        threading.Thread(target=bridge.ask_text,args=(data['prompt'],),daemon=True).start()
                elif self.path=="/ask":
                    text=data.get("text","").strip()
                    if not text or len(text)>2000:raise ValueError()
                    threading.Thread(target=bridge.ask_text,args=(text,),daemon=True).start()
                else:return self.respond(404,b"{}")
                self.respond(200,b"{}")
            except (ValueError,TypeError,RuntimeError) as exc:
                self.respond(400,json.dumps({'error':str(exc)},ensure_ascii=False).encode())
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
