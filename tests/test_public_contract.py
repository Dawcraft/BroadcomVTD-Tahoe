import importlib.util,json,pathlib,re,unittest
R=pathlib.Path(__file__).resolve().parents[1]
class PublicContract(unittest.TestCase):
    def test_no_positive_boot_arguments(self):
        s=(R/'POC/Frontend.cpp').read_text()
        for a in ['-brcmvtd','-brcmvtdrx','-brcmvtdtxchain','-brcmvtdprivatetx300','-brcmvtdnotxcleanup']:
            self.assertNotIn('checkKernelArgument("'+a+'")',s)
        self.assertIn('source!=MapperSource::VerifiedSystemAppleVTD',s)
    def test_caller_not_authority(self):
        s=(R/'POC/MapperCore.cpp').read_text();part=s[s.index('Terminal beginTerminal'):s.index('static bool current')]
        self.assertIn('e.payload[9]=caller',part)
        self.assertNotRegex(part,r'caller\s*[!=]=')
        self.assertIn('if(blockRecords)r.revision=0',s)
    def test_bulk_decoder_metadata(self):
        from test_poc_private import PrivateDecoder,d
        b=PrivateDecoder().blob(9);data=list(range(18));data[9]=0xfb7e3;data[10]=49
        d.EVENT.pack_into(b,88,1,100,1,2,3,4,102,9,*data)
        _,e=d.decode(b);p=e[0]['private_tx']
        self.assertEqual(p['native_reset_caller'],'0xfb7e3')
        self.assertEqual(p['observed_core_revision'],49)
        self.assertFalse(p['caller_is_ownership_authority'])
    def test_exact_seven_read_roles(self):
        s=(R/'Config/TargetGate.hpp').read_text().split('ownershipReadSites[] = {')[1].split('};')[0]
        self.assertEqual(s.count('ReadUse::'),7)
    def test_private_terminal_order(self):
        s=(R/'POC/Frontend.cpp').read_text();s=s[s.index('static bool wrapReset'):s.index('static void wrapTxSync')]
        self.assertLess(s.index('privateTx::beginTerminal'),s.index('quarantineQueue'))
        self.assertLess(s.index('original<bool>(Reset'),s.index('privateTx::finishTerminal'))
    def test_native_fast_paths(self):
        s=(R/'POC/Frontend.cpp').read_text()
        wrappers=re.findall(r'static [^\n]+wrap(\w+)\([^\n]*\) \{\n([^\n]+)',s)
        self.assertEqual(len(wrappers),30)
        for name,first in wrappers:
            if name not in ['Start','ReadReg']:self.assertIn('if(!correctiveMode())',first,name)
