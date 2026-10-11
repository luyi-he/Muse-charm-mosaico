import unittest
from unittest.mock import patch
import numpy as np
from audio_api import speak,check


class AudioTests(unittest.TestCase):
    def test_pcm_resampling_and_streaming(self):
        # 0.4 seconds at 24 kHz must become 0.4 seconds at device rate 16 kHz.
        pcm=(np.sin(np.arange(9600)*2*np.pi*440/24000)*10000).astype('<i2').tobytes()
        played=[];outer=self
        class Response:
            status_code=200
            def __enter__(self):return self
            def __exit__(self,*args):pass
            def iter_bytes(self,size):
                for start in range(0,len(pcm),size):yield pcm[start:start+size]
                outer.assertTrue(played,'playback must start before HTTP completion')
        class Client:
            def __enter__(self):return self
            def __exit__(self,*args):pass
            def stream(self,*args,**kwargs):return Response()
        with patch('audio_api.httpx.Client',return_value=Client()):
            speak('test','test-key','coral',played.append,lambda:True)
        self.assertEqual(len(b''.join(played)),6400*2)

    def test_api_quota_error_is_clear(self):
        class Response:
            status_code=429
            def read(self):pass
            def json(self):return {'error':{'code':'credit_balance_exhausted'}}
        with self.assertRaisesRegex(RuntimeError,'余额'):check(Response(),'朗读')


if __name__=='__main__':unittest.main()
