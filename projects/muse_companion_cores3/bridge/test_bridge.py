"""USB framing regression: serial timeouts must not split JSON records."""
import base64
import asyncio
import collections
import json
import queue
import threading
import unittest
from unittest.mock import patch
import serial
from bridge import Bridge, qwen_tts


class SerialChunks:
    def __init__(self,chunks):self.chunks=iter(chunks)
    def read_until(self,end):
        try:return next(self.chunks)
        except StopIteration:raise serial.SerialException("test finished")


class FramingTest(unittest.TestCase):
    def test_partial_line_timeout_and_empty_diagnostics(self):
        pcm=b"\x01\x02"*100
        def frame(kind,**fields):
            return b"@cores3 "+json.dumps(dict(type=kind,generation=7,**fields)).encode()+b"\n"
        record=frame("pcm",data=base64.b64encode(pcm).decode())
        bridge=Bridge.__new__(Bridge)
        bridge.alive=True;bridge.logs=collections.deque(maxlen=40)
        bridge.gen=0;bridge.pcm=bytearray();bridge.jobs=queue.Queue(2)
        bridge.serial=SerialChunks([b"\n",b"device log\n",frame("recording"),
            record[:31],b"",record[31:],frame("recording_end")])
        bridge.reconnect=lambda: setattr(bridge,"alive",False)
        bridge.read()
        self.assertEqual(bridge.jobs.get_nowait(),(7,pcm,False))
        self.assertEqual(list(bridge.logs),["device log","USB 连接中断，等待自动重连"])

    def test_usb_reconnect_discards_partial_record_and_recovers_heartbeat(self):
        bridge=Bridge.__new__(Bridge)
        bridge.alive=True;bridge.connected=True;bridge.gen=7;bridge.pending_text='old question'
        bridge.logs=collections.deque(maxlen=40);bridge.pcm=bytearray(b'old audio')
        bridge.tx=threading.Lock();bridge.jobs=queue.Queue(2);bridge.status='ready'
        class Serial:
            is_open=True;opens=0;reads=0
            def close(self):self.is_open=False
            def open(self):
                self.opens+=1
                if self.opens==1:raise serial.SerialException('not yet available')
                self.is_open=True
            def read_until(self,end):
                self.reads+=1
                if self.reads==1:return b'@cores3 {"type":"hel'
                if self.reads==2:raise serial.SerialException('USB unplugged')
                if self.reads==3:return b'@cores3 {"type":"hello","battery":75}\n'
                bridge.alive=False;return b''
        bridge.serial=Serial()
        with patch('bridge.time.sleep'):bridge.read()
        self.assertEqual(bridge.serial.opens,2)
        self.assertEqual(bridge.battery,75)
        self.assertEqual(bridge.status,'USB Codex 已就绪')
        self.assertIsNone(bridge.pending_text)
        self.assertEqual(bridge.pcm,bytearray())
        self.assertTrue(bridge.jobs.empty(),'old requests must not be replayed')

    def test_qwen_plays_before_response_completion(self):
        played=[]
        class Socket:
            async def __aenter__(self):return self
            async def __aexit__(self,*args):pass
            async def send(self,message):pass
            async def events(self):
                yield json.dumps({"type":"session.updated"})
                yield json.dumps({"type":"response.audio.delta","delta":"AQI="})
                self_outer.assertEqual(played,[b"\x01\x02"])
                yield json.dumps({"type":"response.done","response":{"status":"completed"}})
            def __aiter__(self):return self.events()
        self_outer=self
        with patch("websockets.connect",return_value=Socket()):
            result=asyncio.run(qwen_tts("test","not-a-key",played.append))
        self.assertEqual(result,b"")


if __name__=="__main__":unittest.main()
