# from cgrspy import bootstrap
import sys
import cgrspy.bootstrap
import unittest
import threading

class CISMock:
    def __init__(self, lock):
        self.lock = lock

    def query_interface(self, iface):
        print "In query_interface(%s)" % iface
        return None

    def failure(why):
        print "Integration failed: " + why
        lock.release()

    def success():
        print "Integration successful"
        lock.release()

class TestCGRSPy(unittest.TestCase):
    def setUp(self):
        cgrspy.bootstrap.loadGenericModule('cgrs_cellml')
        self.cellmlBootstrap = cgrspy.bootstrap.fetch('CreateCellMLBootstrap')

    def test_createModel(self):
        mod = self.cellmlBootstrap.createModel("1.1")
        self.assertTrue(mod != 0)

    def test_createModelInvalidVersion(self):
        self.assertRaises(ValueError, self.cellmlBootstrap.createModel, "0.9")

    def test_iterate(self):
        mod = self.cellmlBootstrap.createModel("1.1")
        namelist = ["mycomponent", "yourcomponent", "ourcomponent"]
        for n in namelist:
            comp = mod.createComponent()
            comp.name = n
            mod.addElement(comp)
        i = 0
        for c in mod.allComponents:
            self.assertEqual(namelist[i], c.name)
            i = i + 1
        self.assertEqual(i, len(namelist))

        i = 0
        for c in mod.allComponents:
            self.assertEqual(namelist[i], c.name)
            i = i + 1
        self.assertEqual(i, len(namelist))

    def test_callback(self):
        cgrspy.bootstrap.loadGenericModule('cgrs_cis')
        cgrspy.bootstrap.loadGenericModule('cgrs_telicems')
        
        mod = self.cellmlBootstrap.createModel("1.1")

        c = mod.createComponent()
        c.name = "mycomponent"
        mod.addElement(c)

        vx = mod.createCellMLVariable()
        vx.name = "x"
        vx.unitsName = "dimensionless"
        vx.initialValue = "1.0"
        c.addElement(vx);

        vtime = mod.createCellMLVariable()
        vtime.name = "time"
        vtime.unitsName = "dimensionless"
        c.addElement(vtime);

        telicems = cgrspy.bootstrap.fetch('CreateTeLICeMService')
        od = mod.domElement.ownerDocument
        tr = telicems.parseMaths(od, "d(x)/d(time) = x")
        mr = tr.mathResult
        c.addMath(mr)

        cis = cgrspy.bootstrap.fetch('CreateIntegrationService')
        solrun = cis.createODEIntegrationRun(cis.compileModelODE(mod))
        stepType = lambda: ()
        stepType.asString = "ADAMS_MOULTON_1_12"
        solrun.stepType = stepType;
        solrun.setResultRange(0, 10, 0.1)
        lock = threading.Lock()
        lock.acquire()
        solrun.setProgressObserver(CISMock(lock))
        solrun.start()

        lock.acquire()

        pass

def runTests():
    suite = unittest.TestLoader().loadTestsFromTestCase(TestCGRSPy)
    unittest.TextTestRunner(verbosity=2).run(suite)
