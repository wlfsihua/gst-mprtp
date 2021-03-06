from tests import *
from testctrlerbuilder import *
from evaluator import *
from time import sleep
import gzip

import signal
import sys
import argparse

from plotter import *
import threading


class Tester:
    def __init__(self):
        signal.signal(signal.SIGINT, self.on_exit_signal)
        signal.signal(signal.SIGTERM, self.on_exit_signal)
        self.__target_dir = None
        self.__test = None
        self.__test_ctrler = None
        self.__exit_signal_hit = 0
        self.__duration = 0
        self.__timeout = 10
        self.__end = False
        self.__checker_active = False

    def __checker(self):
        if self.__end is True:
            return
        if self.__timeout < 1:
            print('Self Stop checker found an issue!')
            self.__stop_test(self.__test)
            self.__checker_active = False
            return
        timeout = self.__timeout
        self.__timeout = 0
        print('Self Stop checker has a timeout of', timeout)
        threading.Timer(timeout, self.__checker).start()

    def on_exit_signal(self, signum, frame):
        if 0 < self.__exit_signal_hit:
            sys.exit(0)
        self.__exit_signal_hit = self.__exit_signal_hit + 1
        self.__stop_test(self.__test)
        sys.exit(0)

    def setup(self, algorithm, type, params):
        self.__target_dir = params.target
        tcp_flag = False if params.tcp < 1 else True
        self.__test = self.__make_test(type, algorithm, params.latency, params.jitter, params.source, params.sink,
                                       params.subflows_num, tcp_flag)

        self.__test_ctrler = TestCtrlerBuilder.make(self.__test)
        self.__duration = self.__test.duration + self.__test_ctrler.get_max_source_to_sink_delay() + 2

    def start(self):
        subprocess.call("./statsrelayer.out -d &", shell=True)
        sleep(1)
        self.__end = False
        self.__timeout = self.__duration + 60
        self.__checker()
        threading.Thread(target=self.__test_ctrler.start).start()
        sleep(self.__duration)
        self.__stop_test(self.__test)

    def is_ended(self):
        return self.__end

    def __stop_test(self, test):
        self.__test_ctrler.stop()
        command = 'sudo pkill tcpdump'
        subprocess.call(command, shell=True)

        command = 'sudo pkill --signal SIGTERM statsrelayer'
        # Statsrelayer stop
        # command = 'echo "fls *;ext;" >> /tmp/statsrelayer.cmd.in'
        subprocess.call(command, shell=True)
        sleep(3)
        target_dir = 'temp/'
        evaluator = Evaluator(target_dir=target_dir)
        plotter = Plotter(target_dir=target_dir)
        evaluator.setup(self.__test)
        plotter.generate(self.__test)
        print(self.__test.get_descriptions())
        self.__end = True

    def __make_test(self, type, algorithm, latencies, jitters, source_type, sink_type, subflows_num, tcp_flag):
        result = None
        if (type == "rmcat1"):
            result = RMCAT1(
                algorithm=algorithm,
                latency=latencies[0],
                jitter=jitters[0],
                source_type=source_type,
                sink_type=sink_type
            )
        elif (type == "rmcat2"):
            result = RMCAT2(
                algorithm=algorithm,
                latency=latencies[0],
                jitter=jitters[0],
                source_type=source_type,
                sink_type=sink_type
            )
        elif (type == "rmcat3"):
            result = RMCAT3(
                algorithm=algorithm,
                latency=latencies[0],
                jitter=jitters[0],
                source_type=source_type,
                sink_type=sink_type
            )
        elif (type == "rmcat4"):
            result = RMCAT4(
                algorithm=algorithm,
                latency=latencies[0],
                jitter=jitters[0],
                source_type=source_type,
                sink_type=sink_type
            )
        elif (type == "rmcat6"):
            result = RMCAT6(
                algorithm=algorithm,
                latency=latencies[0],
                jitter=jitters[0],
                source_type=source_type,
                sink_type=sink_type
            )
        elif (type == "rmcat7"):
            result = RMCAT7(
                algorithm=algorithm,
                latency=latencies[0],
                jitter=jitters[0],
                source_type=source_type,
                sink_type=sink_type
            )
        elif (type == "mprtp1"):
            result = MPRTP1(
                algorithm=algorithm,
                latency=latencies[0],
                jitter=jitters[0],
                source_type=source_type,
                sink_type=sink_type,
                subflows_num=subflows_num
            )
        elif (type == "mprtp2"):
            result = MPRTP2(
                algorithm=algorithm,
                latency=latencies[0],
                jitter=jitters[0],
                source_type=source_type,
                sink_type=sink_type,
                subflows_num=subflows_num
            )
        elif (type == "mprtp3"):
            result = MPRTP3(
                algorithm=algorithm,
                latency=latencies[0],
                jitter=jitters[0],
                source_type=source_type,
                sink_type=sink_type,
                subflows_num=subflows_num
            )
        elif (type == "mprtp4"):
            result = MPRTP4(
                algorithm=algorithm,
                latency=latencies[0],
                jitter=jitters[0],
                source_type=source_type,
                sink_type=sink_type,
                subflows_num=subflows_num
            )
        elif (type == "mprtp5"):
            result = MPRTP5(
                algorithm=algorithm,
                latency=latencies[0],
                jitter=jitters[0],
                source_type=source_type,
                sink_type=sink_type,
                subflows_num=subflows_num,
                tcp=tcp_flag
            )
        elif (type == "mprtp6"):
            result = MPRTP6(
                algorithm=algorithm,
                latency=latencies[0],
                jitter=jitters[0],
                source_type=source_type,
                sink_type=sink_type,
                subflows_num=subflows_num,
                tcp=tcp_flag
            )
        elif (type == "mprtp7"):
            result = MPRTP7(
                algorithm=algorithm,
                latency=latencies[0],
                jitter=jitters[0],
                source_type=source_type,
                sink_type=sink_type,
                subflows_num=subflows_num,
                tcp=tcp_flag
            )
        elif (type == "mprtp8"):
            result = MPRTP8(
                algorithm=algorithm,
                latency=latencies[0],
                jitter=jitters[0],
                source_type=source_type,
                sink_type=sink_type,
                subflows_num=subflows_num
            )
        elif (type == "mprtp9"):
            result = MPRTP9(
                algorithm=algorithm,
                latency=latencies[0],
                jitter=jitters[0],
                source_type=source_type,
                sink_type=sink_type,
                subflows_num=subflows_num
            )
        elif (type == "mprtp10"):
            result = MPRTP10(
                algorithm=algorithm,
                latency=latencies[0],
                jitter=jitters[0],
                source_type=source_type,
                sink_type=sink_type,
                subflows_num=subflows_num
            )
        elif (type == "mprtp11"):
            result = MPRTP11(
                algorithm=algorithm,
                latency=latencies[0],
                jitter=jitters[0],
                source_type=source_type,
                sink_type=sink_type,
                subflows_num=subflows_num
            )
        else:
            raise ValueError("Unknown value: " + type)
        return result


parser = argparse.ArgumentParser()
parser.add_argument("types", help="The type of the test",
                    choices=['rmcat1', 'rmcat2', 'rmcat3', 'rmcat4', 'rmcat5', 'rmcat6', 'rmcat7',
                             'mprtp1', 'mprtp2', 'mprtp3', 'mprtp4', 'mprtp5', 'mprtp6', 'mprtp7',
                             'mprtp8', 'mprtp9', 'mprtp10', 'mprtp11'], nargs='+')
parser.add_argument("-l", "--latency", help="The late ncy of the path", type=int, nargs='+', choices=[50, 100, 150, 300],
                    default=[50])
parser.add_argument("-j", "--jitter", help="The jitter for the test", type=int, nargs='+', default=[0])
parser.add_argument("-a", "--algorithm", help="The algorithm for the test", default="FRACTaL",
                    choices=["FRACTaL", "SCReAM"])
parser.add_argument("-r", "--runs", help="The runtimes", type=int, default=1)
parser.add_argument("-t", "--target", help="The target directory", default="temp/")
# default_video = "FILE:Kristen.yuv:1:1280:720:2:60/1"
default_video = "FILE:Kristen.yuv:1:1280:720:2:25/1"
# default_video = "FILE:Kristen_1024.yuv:1:1024:576:2:60/1"
# default_video = "FILE:out.yuv:1:320:240:2:60/1" # Kristen
# default_video = "FILE:out2.yuv:1:320:240:2:60/1" # Park
# default_video = "FILE:foreman_cif.yuv:1:352:288:2:20/1"
parser.add_argument("-s", "--source", help="The source type format", default=default_video)
parser.add_argument("-i", "--sink", help="The sink type format", default="FAKESINK")
parser.add_argument("-u", "--subflows_num", help="The number of subflows we want", default=2, type=int)
parser.add_argument("--tcp", help="TCP Indicator", default=0, type=int)

parser.add_argument("--vqmt", help="Calculate metrics video quality metrics using VQMT after the run", default=None)
vqmt_script = ""
vqmt_target = "video_quality/kristen"

args = parser.parse_args()

root = logging.getLogger()
root.setLevel(logging.DEBUG)

ch = logging.StreamHandler(sys.stdout)
ch.setLevel(logging.DEBUG)
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
ch.setFormatter(formatter)
root.addHandler(ch)
algorithm = Algorithms.FRACTaL
if (args.algorithm == "SCReAM"):
    algorithm = Algorithms.SCReAM

print('Run test %s, algorithm: %s by %d times with [%s] path latency and jitter [%s] on %d subflows and TCP flag is %d' %
      (args.types, str(algorithm.name), args.runs, ", ".join(map(str, args.latency)), ", ".join(map(str, args.jitter)),
       args.subflows_num, args.tcp))

import string
import random


def id_generator(size=6, chars=string.ascii_uppercase + string.digits):
    return ''.join(random.choice(chars) for _ in range(size))


running_id = id_generator()

from shutil import move

moving = 1 < args.runs or 1 < len(args.types)
for type_ in args.types:
    for run in range(args.runs):
        tester = Tester()
        tester.setup(algorithm, type_, args)
        tester.start()
        while tester.is_ended() is False:
            print("Test has not been finished")
            sleep(10)

        # TODO: implement here the VQMT part

        if moving is False:
            continue
        dirname = 'temp_super/' + type_ + '/' + \
                  '_'.join([
                      args.algorithm,
                      running_id,
                      str(run).lower(),
                      '_'.join(list(map(lambda x: str(x) + "ms", args.latency))),
                      '_'.join(list(map(lambda x: str(x) + "ms", args.jitter))),
                      str(args.subflows_num),
                  ])
        if not os.path.isdir(dirname):
            os.makedirs(dirname)
        else:
            folder = dirname
            for the_file in os.listdir(folder):
                file_path = os.path.join(folder, the_file)
                try:
                    if os.path.isfile(file_path):
                        os.unlink(file_path)
                        # elif os.path.isdir(file_path): shutil.rmtree(file_path)
                except Exception as e:
                    print(e)
        folder = 'temp'
        for the_file in os.listdir(folder):
            file_path = os.path.join(folder, the_file)
            try:
                if os.path.isfile(file_path):
                    move(file_path, dirname)
                    print("Move ", file_path, "under", dirname)
            except Exception as e:
                print(e)
                # move('temp/*', dirname)
