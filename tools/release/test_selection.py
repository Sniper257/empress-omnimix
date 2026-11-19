"""Installed-set pruning keeps fallback data and exact visible coverage."""
import unittest,json,tempfile
from pathlib import Path
from audit_selection_capacity import audit
from megamix import active_songs,indexed_songs,coverage_text

class SelectionTests(unittest.TestCase):
    def state(self):
        selection={
            'id:1':dict(rank=1,source_id='01',charts=[dict(group='native',preferred=1,column=0)]),
            'third:1':dict(rank=3,source_id='02',charts=[dict(group='shared',preferred=1,column=0)]),
            'red:1':dict(rank=11,source_id='03',charts=[dict(group='shared',preferred=1,column=0)]),
            'third:2':dict(rank=3,source_id='04',charts=[dict(group='unique',preferred=1,column=0)]),
        }
        return {'packages':{'id':{'songs':[dict(source='id',sid=1,style='old',dir='id_1')],'selection':selection},'third':{'songs':[dict(source='third',sid=1,style='old',dir='third_1'),dict(source='third',sid=2,style='old',dir='third_2')]},'red':{'songs':[dict(source='red',sid=1,style='new',dir='red_1')]}}}
    def keys(self,s):return {(r['source'],r['sid'])for r in active_songs(s)}
    def test_fully_covered_rows_retained_as_payload_but_not_resident(self):
        s=self.state();self.assertEqual(self.keys(s),{('id',1),('red',1),('third',2)})
        self.assertEqual(len(s['packages']['third']['songs']),2)
        del s['packages']['red'];self.assertIn(('third',1),self.keys(s))
    def test_variant_parent_and_native_rows_stay(self):
        s=self.state();s['packages']['id']['selection']['third:2']['peers']=['third:1']
        self.assertIn(('third',1),self.keys(s))
        s['packages']['id']['selection']['id:1']['charts']=[]
        self.assertIn(('id',1),self.keys(s))
    def test_package_order_does_not_affect_selection(self):
        s=self.state();expected=self.keys(s);s['packages']=dict(reversed(list(s['packages'].items())))
        self.assertEqual(self.keys(s),expected)

    def test_cross_folder_fallback_stays_indexed_and_has_a_condition(self):
        state=self.state();indexed=indexed_songs(state)
        self.assertEqual(len(indexed),4)
        text=coverage_text(state,indexed,['old','new']);self.assertIn('D 0 third_1 2',text)
        # The third-style row reappears with only its own folder enabled.
        self.assertIn(('third',1),{(r['source'],r['sid'])for r in active_songs(state,{'old'})})
        self.assertIn('D 0 ',text);self.assertTrue(text.strip().endswith(' 2'))
        state['packages']=dict(reversed(list(state['packages'].items())))
        self.assertEqual(coverage_text(state,indexed_songs(state),['old','new']),text)

    def test_finite_gray_code_audit_matches_direct_fixture(self):
        state=self.state();plan={'selection':state['packages']['id']['selection'],'discs':[]}
        for source,p in state['packages'].items():
            plan['discs'].append({'source':source,'targets':{'console'if source=='id'else'append':{'songs':p['songs']}}})
        with tempfile.TemporaryDirectory()as tmp:
            root=Path(tmp);(root/'release-plan.json').write_text(json.dumps(plan));report=audit(root)
            self.assertEqual(report['installed_sets'],4);self.assertEqual(report['max_active_rows'],3)

if __name__=='__main__':unittest.main()
