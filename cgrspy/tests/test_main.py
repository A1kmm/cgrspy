# from cgrspy import bootstrap
import sys
import cgrspy.bootstrap
import unittest
import threading

class CISMock:
    def __init__(self, codeInfo, lock):
        self.lock = lock
        self.success = False
        self.codeInfo = codeInfo
        cti = self.codeInfo.iterateTargets()
        self.ctMap = {}
        while True:
            ct = cti.nextComputationTarget()
            if ct == None:
                break
            if ct.type.asString == "VARIABLE_OF_INTEGRATION":
                offs = 0
            elif ct.type.asString == "CONSTANT":
                continue
            elif ct.type.asString == "STATE_VARIABLE":
                offs = 1
            elif ct.type.asString == "ALGEBRAIC":
                offs = 1 + self.codeInfo.rateIndexCount * 2
            elif ct.type.asString == "FLOATING":
                continue
            elif ct.type.asString == "LOCALLY_BOUND":
                continue
            else:
                raise ("Unexpected computation target type " + ct.type.asString)
            if ct.degree == 0:
                self.ctMap[ct.variable.name + '\'' * ct.degree] = ct.assignedIndex + offs
            self.ctSize = 2 * self.codeInfo.rateIndexCount + 1 + self.codeInfo.algebraicIndexCount

    def results(self, state):
        for i in range(0, len(state), self.ctSize):
            if abs(state[self.ctMap['time'] + i] - 10.0) < 1E-3:
                self.success = abs(state[self.ctMap['x'] + i] - 22026.497973264843) < 1E-3

    def failed(self, why):
        self.lock.release()

    def done(self):
        self.lock.release()

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
        cgrspy.bootstrap.loadGenericModule('cgrs_xpcom')
        cgrspy.bootstrap.loadGenericModule('cgrs_cis')
        cgrspy.bootstrap.loadGenericModule('cgrs_ccgs')
        cgrspy.bootstrap.loadGenericModule('cgrs_telicems')

        mod = self.cellmlBootstrap.createModel("1.1")

        c = mod.createComponent()
        c.name = "mycomponent"
        mod.addElement(c)

        vx = mod.createCellMLVariable()
        vx.name = "x"
        vx.unitsName = "dimensionless"
        vx.initialValue = "1.0"
        c.addElement(vx)

        vtime = mod.createCellMLVariable()
        vtime.name = "time"
        vtime.unitsName = "dimensionless"
        c.addElement(vtime)

        telicems = cgrspy.bootstrap.fetch('CreateTeLICeMService')
        od = mod.domElement.ownerDocument
        tr = telicems.parseMaths(od, "d(x)/d(time) = x")
        mr = tr.mathResult
        c.addMath(mr)

        cis = cgrspy.bootstrap.fetch('CreateIntegrationService')
        compmod = cis.compileModelODE(mod)
        solrun = cis.createODEIntegrationRun(compmod)
        stepType = lambda: ()
        stepType.asString = "ADAMS_MOULTON_1_12"
        solrun.stepType = stepType
        solrun.setResultRange(0, 10, 0.1)
        lock = threading.Lock()
        lock.acquire()
        mock = CISMock(compmod.codeInformation, lock)
        solrun.setProgressObserver(mock)
        solrun.start()
        lock.acquire()
        self.assertEqual(True, mock.success)

def runTests():
    suite = unittest.TestLoader().loadTestsFromTestCase(TestCGRSPy)
    unittest.TextTestRunner(verbosity=2).run(suite)

def test_suite():
    suite = unittest.TestSuite()
    suite.addTest(unittest.makeSuite(TestCGRSPy))
    return suite
