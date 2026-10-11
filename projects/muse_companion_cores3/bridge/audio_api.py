"""OpenAI speech APIs, separate from the CLI's ChatGPT authentication."""
import io
import wave
import av
import httpx
import numpy as np


def check(response,action):
    if response.status_code==200:return
    response.read()
    try:code=response.json().get('error',{}).get('code') or 'unknown'
    except ValueError:code='unknown'
    if code in ('insufficient_quota','credit_balance_exhausted'):raise RuntimeError('OpenAI API 余额或额度不足，请补充 API 余额')
    raise RuntimeError(f'OpenAI {action}请求失败（HTTP {response.status_code}, {code}）')


def transcribe(pcm,key):
    buffer=io.BytesIO()
    with wave.open(buffer,'wb') as wav:
        wav.setnchannels(1);wav.setsampwidth(2);wav.setframerate(16000);wav.writeframes(pcm)
    with httpx.Client(timeout=90) as client:
        response=client.post('https://api.openai.com/v1/audio/transcriptions',
            headers={'Authorization':'Bearer '+key},data={'model':'gpt-4o-mini-transcribe','language':'zh',
                'prompt':'中文对话，可能提到 Codex、CoreS3、任务、会话、聊天。'},
            files={'file':('speech.wav',buffer.getvalue(),'audio/wav')})
        check(response,'识别')
        return response.json().get('text','').strip()


def speak(text,key,voice,on_audio,current):
    resampler=av.AudioResampler(format='s16',layout='mono',rate=16000)
    def output(data):
        frame=av.AudioFrame.from_ndarray(np.frombuffer(data,dtype='<i2').reshape(1,-1),format='s16',layout='mono')
        frame.sample_rate=24000
        for result in resampler.resample(frame):on_audio(bytes(result.planes[0])[:result.samples*2])
    with httpx.Client(timeout=90) as client:
        with client.stream('POST','https://api.openai.com/v1/audio/speech',headers={'Authorization':'Bearer '+key},
                json={'model':'gpt-4o-mini-tts','voice':voice,'input':text[:4000],'response_format':'pcm',
                    'instructions':'用自然、温柔、清晰的中文讲话，像与朋友交谈。'}) as response:
            check(response,'朗读')
            tail=b''
            for chunk in response.iter_bytes(3072):
                if not current():return
                chunk=tail+chunk;even=len(chunk)&~1;tail=chunk[even:]
                if even:output(chunk[:even])
            for result in resampler.resample(None):on_audio(bytes(result.planes[0])[:result.samples*2])
