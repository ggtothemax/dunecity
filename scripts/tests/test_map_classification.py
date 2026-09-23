"""Content-derived categories used by the metaserver map seeder."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('map_seeder',Path(__file__).resolve().parents[1]/'seed-metaserver-maps.py')
seeder=importlib.util.module_from_spec(spec)
spec.loader.exec_module(seeder)

class ClassificationTests(unittest.TestCase):
    def category(self,content):
        return seeder.infer_mod(seeder.ini(content))

    def test_tags_units_and_comments_do_not_classify(self):
        self.assertEqual('vanilla',self.category('[BASIC]\nName=Desert\nMod=dunecity\n[UNITS]\nID0=Atreides,Rocket Trike,256,3\n[STRUCTURES]\n; ID1=Atreides,Nuclear,256,4\nID2=Atreides,Refinery,256,6\n'))

    def test_building_aliases_and_mixed_precedence(self):
        for name,expected in [('Police','dunecity'),('Zone Industrial','dunecity'),('Road','dunecity'),
                              ('TechCenter','tornie'),('Advanced Wind Trap 2x3','tornie'),('WOR','vanilla')]:
            with self.subTest(name=name):
                self.assertEqual(expected,self.category('[STRUCTURES]\nID0=Atreides, '+name.upper()+' ,256,12\n'))
        self.assertEqual('dunecity',self.category('[STRUCTURES]\nID0=Atreides,Worfinery,256,12\nGEN18=Atreides,Road\n'))

    def test_sparse_city_named_starter_maps(self):
        for count,expected in [(4,'dunecity'),(5,'vanilla')]:
            content='[BASIC]\nName=DuneCity\n[Player1]\n[Player2]\n[STRUCTURES]\n'
            content+=''.join('ID%d=Atreides,Const Yard,256,%d\n'%(i,i*10) for i in range(count))
            self.assertEqual(expected,self.category(content))
        self.assertEqual('dunecity',seeder.infer_mod(seeder.ini('[MAP]\nSizeX=64\nSizeY=64\n'),'Twin Cities'))

    def test_non_structure_sections_and_invalid_keys_are_ignored(self):
        self.assertEqual('vanilla',self.category('[BASIC]\nDescription=Atreides,Nuclear,256,12\n[STRUCTURES]\nDescription=Atreides,Nuclear,256,12\n'))

if __name__=='__main__':unittest.main()
