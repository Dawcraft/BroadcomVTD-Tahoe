import hashlib
import importlib.util
import json
import pathlib
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
s=importlib.util.spec_from_file_location('quiet_decoder',ROOT/'tools/decode_poc.py')
d=importlib.util.module_from_spec(s);s.loader.exec_module(d)
class QuiescenceDecoder(unittest.TestCase):
    def record(self,stage,values):
        b=bytearray(d.HEADER.pack(0x3154434152545642,13,200,32768,6,1,0,0,0,1,2,bytes(16)))
        b+=bytes((32768+65+1025+1)*200)
        d.EVENT.pack_into(b,88,1,100,1,2,3,4,101,stage,*(values+[0]*(18-len(values))))
        d.EVENT.pack_into(b,len(b)-200,0,0,0,0,0,0,90,0,*([0]*18))
        return d.decode(b)
    def test_idle_is_observation_not_permission(self):
        h,e=self.record(2,[1,3,0x1061cd,4,0x10000000,0x20000000,2,1,12])
        q=e[0]['tx_quiescence'];self.assertFalse(q['ownership_permission']);self.assertFalse(q['native_disposition_executed'])
        self.assertIn('status0_high_nibble_2',q['seen_names']);self.assertFalse(h['first_failure']['captured'])
    def test_flush_return_not_success(self):
        _,e=self.record(3,[1,2,63,48]);self.assertIn('return alone is not success',e[0]['tx_quiescence']['warning'])
    def test_coverage_is_unknown_not_halt(self):
        h,e=self.record(8,[1,5,3,4,64]);self.assertEqual(e[0]['tx_quiescence']['coverage_name'],'record budget')
        self.assertFalse(h['first_failure']['captured'])
    def test_control_entry_not_completed_write(self):
        _,e=self.record(6,[1,13,0,2,10,42]);self.assertFalse(e[0]['tx_quiescence']['returned'])
    def test_serial_context_not_disposition(self):
        _,e=self.record(5,[1,2,63,48,0,3,0x1000,12,8,0,12,11,0,0,364,0x14709f,1,0])
        q=e[0]['tx_quiescence'];self.assertEqual(q['serial'],364);self.assertFalse(q['native_disposition_executed'])
if __name__=='__main__':unittest.main()
