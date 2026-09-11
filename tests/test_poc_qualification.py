import hashlib
import importlib.util
import json
import pathlib
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('qual_decoder',ROOT/'tools/decode_poc.py')
d=importlib.util.module_from_spec(spec);spec.loader.exec_module(d)

class QualificationDecoder(unittest.TestCase):
    def capture(self,typ,flags,payload):
        b=bytearray(d.HEADER.pack(0x3154434152545642,13,200,32768,6,1,0,0,0,1,2,bytes(16)))
        b+=bytes((32768+65+1025+1)*200)
        d.EVENT.pack_into(b,88,1,100,1,2,3,4,typ,flags,*(payload+[0]*(18-len(payload))))
        d.EVENT.pack_into(b,len(b)-200,0,0,0,0,0,0,90,0,*([0]*18))
        return d.decode(b)
    def test_entry_first_reason_and_no_failure_authority(self):
        h,e=self.capture(98,1,[1,9,0x14709f,1,8,0,(1<<1)|(1<<3)])
        q=e[0]['tx_qualification']
        self.assertEqual(q['entry_reasons_names'],['caller_mismatch','control_flag'])
        self.assertEqual(q['entry_reasons_first'],'caller_mismatch')
        self.assertFalse(q['ownership_permission']);self.assertFalse(h['first_failure']['captured'])
        self.assertEqual(h['stop_after'],0)
    def test_preselection_counterfactual_is_labeled(self):
        _,e=self.capture(98,2,[1,9,0,9,(1<<9)|(1<<11)])
        q=e[0]['tx_qualification'];self.assertEqual(q['independent_pre_reasons_first'],'start_index')
        self.assertIn('NOT proof',q['warning'])
    def test_post_clear_failure_is_not_permission(self):
        _,e=self.capture(98,3,[1,9,1<<18]);q=e[0]['tx_qualification']
        self.assertEqual(q['independent_post_reasons_first'],'descriptor_span_not_cleared')
        self.assertFalse(q['ownership_permission'])
    def test_requeue_capable_not_executed_requeue(self):
        _,e=self.capture(98,4,[1,9,1,0,0,0,0,1,1,3]);q=e[0]['tx_qualification']
        self.assertEqual(q['native_context'],'native sync requeue-capable branch')
        self.assertIn('NOT an executed',q['warning'])
        self.assertTrue(q['tracked_return_withheld'])
    def test_free_context_not_new_certificate(self):
        _,e=self.capture(98,4,[1,9,1,0,0,0,0,1,1,1]);q=e[0]['tx_qualification']
        self.assertFalse(q['certificate_present']);self.assertFalse(q['ownership_permission'])
    def test_span_metadata_and_missing_observation(self):
        _,e=self.capture(99,0,[1,9,0,123,0,456,789,0,0,7,0])
        q=e[0]['tx_qualification_span'];self.assertEqual(q['pre_packet'],123)
        self.assertFalse(q['post_observed']);self.assertFalse(q['ownership_permission'])
    def test_coverage_is_not_halt(self):
        h,e=self.capture(98,7,[32,2]);self.assertEqual(e[0]['tx_qualification']['coverage'],'tracked qualification budget reached')
        self.assertFalse(h['first_failure']['captured'])
    def test_pre_mapping_is_distinct_from_returned_mapping(self):
        _,e=self.capture(98,9,[1,12,11,123,1,2,7,1,999,4,1,1,1<<11])
        q=e[0]['tx_qualification'];self.assertEqual((q['returned_serial'],q['pre_serial']),(12,11))
        self.assertEqual(q['independent_pre_reasons_names'],['map_record'])
    def test_reset_ack_does_not_override_missing_software_certificate(self):
        _,e=self.capture(98,5,[1,9,34,1,34,0,0,0,0,0,1,1,6,0,1,42,34293,1])
        q=e[0]['tx_qualification'];self.assertEqual(q['missing_requirements'],['not_detached','different_detach_thread','not_notified'])
        self.assertFalse(q['ownership_permission'])
    def test_lifecycle_is_attempt_not_original_execution(self):
        _,e=self.capture(98,8,[0,9,2,6,1]);q=e[0]['tx_qualification']
        self.assertEqual(q['action_name'],'wrapped Free attempt');self.assertIn('NOT proof original',q['warning'])

if __name__=='__main__':unittest.main()
