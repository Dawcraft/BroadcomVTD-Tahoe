import importlib.util
import pathlib
import struct
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('decoder_pressure',ROOT/'tools/decode_poc.py')
d=importlib.util.module_from_spec(spec);spec.loader.exec_module(d)

class PressureDecoder(unittest.TestCase):
    def capture(self,version=13):
        b=bytearray(d.HEADER.pack(0x3154434152545642,version,200,32768,6,3,0,0,0,1,2,bytes(16)))
        b+=bytes((32768+65+1025+1)*200)
        for i,typ in enumerate((95,97,96)):
            d.EVENT.pack_into(b,88+i*200,i+1,100+i,1,2,3,4,typ,0,*([1,64]+[0]*16))
        totals=[0,0,18,18,64,0,0,18,0,0,0,2,1,46,1,100,1,0]
        d.EVENT.pack_into(b,88+(32768+64)*200,0,200,1,0,0,0,42,0xffffffff,*totals)
        d.EVENT.pack_into(b,len(b)-200,0,0,0,0,0,0,90,0,*([0]*18))
        return b
    def test_pressure_not_permanent_failure_and_no_size_change(self):
        b=self.capture();h,e=d.decode(b)
        self.assertEqual(len(b),6771888)
        self.assertEqual([x['event'] for x in e],['MapperNoCredit','MapperCreditReturned','MapperAdmissionResumed'])
        self.assertTrue(all(not x['tx_pressure']['permanent_failure'] for x in e))
        self.assertEqual((h['stop_after'],h['tx_totals']['halt_reason']),(0,0))
        self.assertFalse(h['first_failure']['captured'])
        p=h['tx_totals']['pressure']
        self.assertEqual((p['no_credit_events'],p['reservations_after_pressure'],
                          p['normal_completions_after_pressure'],p['submissions_after_pressure']),(2,1,46,1))
        self.assertFalse(p['snapshot_atomic'])
    def test_genuine_halt_remains_separate(self):
        b=self.capture()
        d.EVENT.pack_into(b,len(b)-200,4,400,1,0,0,0,90,18,*([41,0,0,8196,0]+[0]*13))
        h,_=d.decode(b)
        self.assertTrue(h['first_failure']['captured'])
        self.assertEqual(h['first_failure']['reason'],18)
        self.assertEqual(h['tx_totals']['pressure']['no_credit_events'],2)
    def test_old_reserved_fields_not_reinterpreted(self):
        h,_=d.decode(self.capture(12))
        self.assertNotIn('pressure',h['tx_totals'])
    def test_real_frontend_reservation_rejection_precedes_original_tx(self):
        src=(ROOT/'POC/Frontend.cpp').read_text()
        block=src[src.index('static int32_t wrapTx'):src.index('static int32_t wrapUnframed')]
        self.assertIn('if (!m) return reject(1);',block)
        self.assertIn('if (!m || abortUnpublished(*m)) original<void>(Free,osh,packet,uint32_t(1));',block)
        self.assertLess(block.index('if (!m) return reject(1);'),block.index('prepareMapping('))
        self.assertLess(block.index('prepareMapping('),block.rindex('original<int32_t>(Tx'))
    def test_core_no_credit_cannot_create_or_clear_halt(self):
        src=(ROOT/'POC/MapperCore.cpp').read_text()
        reserve=src[src.index('Mapping *reserveMapping'):src.index('void markQuarantine')]
        self.assertIn('mappingsHalted()',reserve)
        self.assertNotIn('haltMappings(',reserve)
        self.assertNotIn('&halted',reserve)
        self.assertNotIn('releasePrepared(',reserve)
        self.assertNotIn('quarantine(',reserve)
        self.assertIn('transition(m,MapState::Empty,MapState::Preparing)',reserve)

if __name__=='__main__':unittest.main()
