"""Gemini speech input and streaming speech output; Codex remains the agent."""
import base64
import io
import json
import wave
import httpx

URL='https://generativelanguage.googleapis.com/v1beta/interactions'


def check(response):
    if response.status_code==200:return
    response.read()
    try:status=response.json().get('error',{}).get('status','unknown')
    except ValueError:status='unknown'
    raise RuntimeError(f'Gemini 音频请求失败（HTTP {response.status_code}, {status}）')


def transcribe(pcm,key):
    buffer=io.BytesIO()
    with wave.open(buffer,'wb') as wav:
        wav.setnchannels(1);wav.setsampwidth(2);wav.setframerate(16000);wav.writeframes(pcm)
    payload={'model':'gemini-3.5-flash-lite','generation_config':{'thinking_level':'minimal'},'input':[
        {'type':'text','text':'只转写音频中实际说出的内容，不回答，不补写，不解释。中文使用简体，保留 Codex、CoreS3 等名称。无语音则返回空文本。'},
        {'type':'audio','mime_type':'audio/wav','data':base64.b64encode(buffer.getvalue()).decode()}]}
    with httpx.Client(timeout=90) as client:
        response=client.post(URL,headers={'x-goog-api-key':key},json=payload);check(response)
        text=[]
        for step in response.json().get('steps',[]):
            if step.get('type')=='model_output':
                text.extend(c.get('text','') for c in step.get('content',[]) if c.get('type')=='text')
        return ''.join(text).strip()


def speak(text,key,voice,on_audio,current):
    payload={'model':'gemini-3.8-flash-lite-tts','input':[{'type':'user_input','content':[
        {'type':'text','text':text[:4000],'annotations':[{'type':'speech_metadata','style':'自然、温柔、清晰的中文，像朋友轻松交谈。'}]}]}],
        'response_format':{'type':'audio','mime_type':'audio/l16','sample_rate':16000},
        'generation_config':{'speech_config':[{'voice':voice}]},'stream':True}
    with httpx.Client(timeout=90) as client:
        with client.stream('POST',URL,headers={'x-goog-api-key':key},json=payload) as response:
            check(response);tail=b'';received=False
            for line in response.iter_lines():
                if not current():return
                if not line.startswith('data:'):continue
                raw=line[5:].strip()
                if raw=='[DONE]':break
                event=json.loads(raw);delta=event.get('delta',{})
                if event.get('event_type',event.get('type'))=='interaction.completed':break
                if event.get('event_type')=='error' or event.get('type')=='error':raise RuntimeError('Gemini 音频流返回错误')
                if delta.get('type')=='audio' and delta.get('data'):
                    chunk=tail+base64.b64decode(delta['data']);even=len(chunk)&~1;tail=chunk[even:]
                    if even:on_audio(chunk[:even]);received=True
            if not received:raise RuntimeError('Gemini 没有返回音频，请检查模型权限')
