#!/usr/bin/env python

# runShell.py - start a shell over XCPU3
# Copyright 2010 Pravin Shinde
# 

def showUsage () :
    print """
USAGE: runShell.py [-l hare_location] [-9 py9p_location]  
                [-h brasil_host] [-p brasil_port] 
                [-n numnodes] [-o os_type] [-a architecture_type]
                [-s shell_name]
 
ARGUMENTS:
    -l: hare_location default value ["/bgsys/argonne-utils/profiles/plan9/LATEST/bin/"]
    -9: py9p_loation (specify only if py9p is installed separately)
    -D: debug mode
    -h: host running brasil daemon (defaults to current host)
    -p: port running brasil daemon (default: 5670)
    -n: number of cpu nodes to provision (mandetory)
    -o: OS type (default value "any") (supported values: Linux, Plan9, MacOSX, Nt)
    -a: arch type (default value "any") (supported values: 386, arm, power, spim)
    -s: shell name name of the shell to run (NOTE: this option *MUST* be provided)

ENVIRONMENT VARIABLES: (can be used to override defaults in place of cmdline)
	HARE_LOCATION - Place where hare executables are installed
	BRASIL_HOST - host running brasil daemon
	BRASIL_PORT - port running brasil daemon

EXAMPLES
    runShell.py -n 4 -s bash
    runSell.py -l /path/to/hare/binaries/ -n 8 -o Linux -a 386 -s bash
    runJob.py -9 /path/to/py9p/ -n 3 -o Plan9 -s rc
    
NOTES
    To close the session, you should send command that will terminate the shell,
    For example, in case of bash, user should type "exit" and then to terminate
    the input, press "CTRL+D" which will close the input file.
"""


import socket
import sys
import os
import getopt
import getpass
import code
import readline
import atexit
import threading
import thread

# Find the location of py9p
DEFAULTPY9P = "/bgsys/argonne-utils/profiles/plan9/LATEST/bin/"
import getopt
py9pPath = DEFAULTPY9P + "/py9p/"
pathSource = "Default Path ["  + DEFAULTPY9P + "] "

if 'HARE_PATH' in os.environ:
    py9pPath = os.environ['HARE_PATH'] + "/py9p/"
    pathSource = "Environment Variable HARE_PATH ["  + os.environ['HARE_PATH'] + "] "

try:
    opts, args = getopt.getopt (sys.argv[1:],"Dl:9:h:p:n:o:a:s:")
    for o,a in opts:
        if o == "-l" :
            py9pPath = a + "/py9p/"
            pathSource = "cmdline arg -l ["  + a + "] "
        
        if o == "-9" :
            py9pPath = a
            pathSource = "cmdline arg -9 ["  + a + "] "
except getopt.GetoptError:
    pass
    
if not os.path.exists(py9pPath+"/py9p/py9p.py"):
    print "ERROR: Could not find py9p in the location [" \
        + py9pPath + "] specified by : " + pathSource
    print """
    You can do following to solve this problem.
        1. use -l to specify the path to brasil binary executables.
        2. use -9 to specify the path to py9p if it is not inside brasil binaries. 
        3. Set environment variable HARE_PATH pointing to brasil binary executables
        4. Copy the brasil binary executables in default location : """ + DEFAULTPY9P
    showUsage ()
    sys.exit()

#print "Using py9p location [" + py9pPath +"]"
sys.path.append (py9pPath + '/py9p')
try:
    import py9p
except :
    print "ERROR: Could not import py9p"
    print "Please provide path to py9p or hare installation " \
        + "from environment variable or command line parameter"
    sys.exit()
        
remoteURL = ""
#baseURL = "/cmd2/local/"
#baseURL = "/task/remote/"
baseURL = "/csrv/local/"
#baseURL = "/local/"


def catAsync (obj, *args):
    debug = False
    if debug : print "thread started"
    while 1:
        if debug : print "thread reading"
        buf = obj.read(512)
        #buf = obj.readline()
        if debug : print "thread read " + buf
        if len(buf) <= 0:
            break
        sys.stdout.write (buf)

    if debug : print "thread dead"


class XCPU3Client:
    
    sessionID = None
    DEBUG = False
    bufsize = 512
    
    def __init__(self, srv, port, authmode='none', user='', passwd=None, authsrv=None, chatty=0, key=None):
        self.sessionID = None
        self.bufSize = 512
        self.DEBUG = False
        
        self.extraLink = self.get9pClient(srv, port, authmode, user, passwd, authsrv, chatty, key)
        self.inputLink = self.get9pClient(srv, port, authmode, user, passwd, authsrv, chatty, key)
        self.ctlLink = self.get9pClient(srv, port, authmode, user, passwd, authsrv, chatty, key)
        self.outputLink = self.get9pClient(srv, port, authmode, user, passwd, authsrv, chatty, key)
        self.debugLink = self.get9pClient(srv, port, authmode, user, passwd, authsrv, chatty, key)


    def dPrint (self, msg):
        if (self.DEBUG):
            print msg

    def enableDebug (self) :
        name = "/csrv/local/ctl"
        print "enabling debugging"
        if self.debugLink.open (name, py9p.ORDWR) is  None:
            raise Exception ("XCPU3: could not open " + name)
        self.debugLink.write ("debug 1");
        self.debugLink.close ();
        
    def disableDebug (self) :
        name = "/csrv/local/ctl"
        print "disabling debugging"
        if self.debugLink.open (name, py9p.ORDWR) is  None:
            raise Exception ("XCPU3: could not open " + name)
        self.debugLink.write ("debug 0");
        self.debugLink.close ();

    def get9pClient (self, srv, port, authmode='none', user='', passwd=None, authsrv=None, chatty=0, key=None) :
        privkey = None
           
        sock = socket.socket(socket.AF_INET)
        try:
            sock.connect((srv, port),)
        except socket.error,e:
            print "%s: %s" % (srv, e.args[1])
            raise
        return py9p.Client(py9p.Sock(sock, 0, chatty), authmode, user, passwd, authsrv, chatty, key=privkey)        
        
    def cat(self, name, obj=None, out=None):
        if out is None:
            out = sys.stdout
        if obj is None:
            obj = self.extraLink
        if obj.open(name) is None:
            raise Exception("XCPU3: Could not open " + name)
        while 1:
            buf = obj.read(self.bufSize)
            if len(buf) <= 0:
                break
            out.write(buf)
        obj.close()

    def topStat (self, out=None):
        name =  baseURL + "status"
        self.cat(name, None, out)
        
    def getSessionStatus (self, out =  None) :
        if self.sessionID is None :
            print "Error: Session is not started yet"
        
        name = baseURL + str(self.sessionID) + "/status"
        self.cat (name, self.extraLink, out)

    def startSession (self):
        if self.sessionID is not None :
            print "Session already running"
            return self.sessionID
        
        name = baseURL + "clone"
        if self.ctlLink.open (name, py9p.ORDWR) is None :
            raise Exception("XCPU3: Could not open " + name)
        
        buf = self.ctlLink.read(self.bufSize)
        self.sessionID = int (buf)
        return self.sessionID
    
    def requestReservation (self, res) :
        if self.sessionID is None :
            self.startSession()
        if res is None :
            return
        param = "res " + res
        self.ctlLink.write(param)
    
    def requestExecution (self, cmd) :    
        if self.sessionID is None :
            self.startSession()
        param = "exec " + cmd
        self.ctlLink.write(param)
        name = baseURL + str(self.sessionID) + "/stdio"
        
        if self.outputLink.open (name, py9p.OREAD ) is None :
            raise Exception("XCPU3: Could not open " + name)

        if self.inputLink.open(name, py9p.OWRITE) is None:
            raise Exception("XCPU3: Could not open stdio file " + name)

        
    def sendInput (self, input):
        if self.sessionID is None :
            raise Exception("XCPU3: Session not created")

        if input is None :
            return
        if  input == '-' :
            inf = sys.stdin
            self.dPrint ("Using stdin for input")
        else :
            inf = open(input, "r", 0)
        
        sz = self.bufSize
        while 1:
            # buf = inf.read(sz)
            buf = inf.readline()
            if len(buf) <= 0:
                break
            self.dPrint ("sending line [" + buf + "]")
            self.inputLink.write(buf)
        self.dPrint ("input sent")
        inf.close()


    def getOutputAsync (self) :

        if self.sessionID is None :
            raise Exception("XCPU3: Session not created")
        
        try:
            threading.Thread(target=catAsync, args=(self.outputLink, 1)).start()
        except Exception, errtxt:
            print errtxt
            raise Exception
            return
        self.dPrint ("Thread started ...")
        
    def getOutput (self) :
        if self.sessionID is None :
            raise Exception("XCPU3: Session not created")
        while 1:
            buf = self.outputLink.read(self.bufSize)
            if len(buf) <= 0:
                break
            sys.stdout.write(buf)        
    
    def endSession (self) :
        if self.sessionID is None :
            self.dPrint ( "Session already close")
            return
        self.inputLink.close()
        self.ctlLink.close()
        self.outputLink.close()
        self.sessionID = None


            
    def runJob (self, cmd, res = None, input = None ):
        self.dPrint ("Requesting reservation..")
        self.requestReservation(res)
        self.dPrint ( "Requesting execution..")
        self.requestExecution(cmd)
        self.dPrint ( "getting output")
        self.getOutputAsync()

        self.dPrint ( "sending input")

        self.sendInput ("-")
#        self.getOutput()

        if self.DEBUG:
            self.dPrint ( "checking session status")
            self.getSessionStatus ()
        self.dPrint ( "Closing session")
        self.endSession ()
        self.dPrint ( "Done..")
    


def main ():
    # Filling default values to all the variables which are not provided with
    brasilHost = "localhost"
    brasilPort = 5670
    nflag = None
    osType = None
    archType = None
    debug = False
    shellType = None
    
    # Overriding the values from environment variables
    if 'BRASIL_HOST' in os.environ:
        brasilHost = os.environ['BRASIL_HOST']
    
    if 'BRASIL_PORT' in os.environ:
        brasilPort = int (os.environ['BRASIL_PORT'])

    try:
        opts, args = getopt.getopt (sys.argv[1:],"Dl:9:h:p:n:o:a:s:")
        for o,a in opts:
            if o == "-h" :
                brasilHost = a
            elif o == "-p" :
                brasilPort = int (a)
            elif o == "-n" :
                nflag = a
            elif o == "-o" :
                osType = a
            elif o == "-a" :
                archType = a
            elif o == "-s" :
                shellType = a
            elif o  == "-D" :
                debug = True
            elif ( o in ( "-9", "-l" ) ) :
                a = 1 + 1
            else :
                print "ERROR: Wrong option provided " + o
                showUsage ()
                sys.exit ()

        argLen = len (args)
        
        if nflag == None :
            print "ERROR: Number of CPU's needed is not provided"
            showUsage ()
            sys.exit ()
        
        if (int(nflag) < 0 ) :
            print "ERROR: Number of CPU's provided are invalid"
            showUsage ()
            sys.exit ()
        
        resReq = nflag
        
        if osType not in (None, "any", "Any", "ANY", "*" ) :
            resReq = resReq + " " + osType
            if archType not in (None, "any", "Any", "ANY", "*" ) :
                resReq = resReq + " " + archType
            
            
            
        if shellType == None :
            print "ERROR: shell name not provided"
            showUsage ()
            sys.exit ()
            
    except getopt.GetoptError:
        print "exception"
        showUsage ()
        sys.exit()


    # acutal code responsible for working
    mycpu = XCPU3Client (brasilHost, brasilPort)
    mycpu.DEBUG = debug
    if mycpu.DEBUG :
        mycpu.enableDebug()
        print "Top statistics is"
        mycpu.topStat()
    
    mycpu.runJob (shellType, resReq )
    
    if mycpu.DEBUG :
        mycpu.disableDebug()
    
if __name__ == "__main__" :
    main ()
        
