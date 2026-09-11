import importlib.util,pathlib,unittest
R=pathlib.Path(__file__).resolve().parents[1]
s=importlib.util.spec_from_file_location('private_decoder',R/'tools/decode_poc.py')
d=importlib.util.module_from_spec(s);s.loader.exec_module(d)
class PrivateDecoder(unittest.TestCase):
    def blob(self,stage):
        b=bytearray(d.HEADER.pack(0x3154434152545642,13,200,32768,6,1,0,0,0,1,2,bytes(16)))
        b+=bytes((32768+65+1025+1)*200)
        d.EVENT.pack_into(b,88,1,100,1,2,3,4,102,stage,*range(18))
        d.EVENT.pack_into(b,len(b)-200,0,0,0,0,0,0,90,0,*([0]*18))
        return b
    def test_assumption_explicit(self):
        for stage in [11,12,13]:
            h,e=d.decode(self.blob(stage));p=e[0]['private_tx']
            self.assertIn('EXPERIMENTAL',p['stage']);self.assertIn('NOT REV49 SOURCE-PROVEN',p['hardware_contract'])
            self.assertFalse(h['first_failure']['captured'])
    def test_old_snapshot_not_packet_ownership(self):
        b=self.blob(3);data=[4,6,3,2,800,5,7,0x1000,1,1,0,1,1,99,0x5000,0,0,7]
        d.EVENT.pack_into(b,88+32768*200,0,0,0,2,0x1234,0x2000,42,0x40000000,*data)
        totals=[0]*18;totals[17]=(1<<63)|1
        d.EVENT.pack_into(b,88+(32768+64)*200,0,0,0,0,0,0,42,0xffffffff,*totals)
        h,_=d.decode(b);p=h['mapping_snapshot'][0]
        self.assertTrue(p['pending_terminal_generation']);self.assertEqual(p['active_packet_association'],'0x0')
        self.assertEqual(p['generation'],99);self.assertNotIn('detached_caller',p)
        self.assertEqual(h['tx_totals']['private_pending_old'],1)
    def test_return_not_requeue_prediction(self):
        _,e=d.decode(self.blob(4));self.assertEqual(e[0]['private_tx']['stage'],'exact_packet_returned_native')
        self.assertTrue(e[0]['private_tx']['pointer_reuse_is_not_proof_of_requeue'])
    def test_reset_identity(self):
        _,e=d.decode(self.blob(9));p=e[0]['private_tx'];self.assertEqual(p['ticket'],3)
        self.assertEqual(p['old_generation'],1);self.assertEqual(p['new_generation'],2)
if __name__=='__main__':unittest.main()
