import serial
import time
import urllib.request
import json
import threading
from queue import Queue

url = "http://192.168.31.28/timestamp"

interval = 0.75
rt_delay = -1
n = 255 * 10
#ser = serial.Serial('/dev/tty.SLAB_USBtoUART', 115200, timeout=1)

q = Queue()

def post_url(url, body):
    req = urllib.request.Request(url)
    req.add_header('Content-Type', 'application/json; charset=utf-8')
    jsondata = json.dumps(body)
    jsondataasbytes = jsondata.encode('utf-8')   # needs to be bytes
    req.add_header('Content-Length', len(jsondataasbytes))
    start = time.time()
    response = urllib.request.urlopen(req, jsondataasbytes)
    body['rt_delay'] = int(round((time.time() - start) * 1000))
    q.put(body)


delays = []
ts = 0
for i in range(n):
    #body = {'timestamp': int(round(time.time()*1000)), 'delay':rtdelay}
    body = {'timestamp': ts, 'delay':rt_delay}

    start = time.time()
    thread = threading.Thread(target=post_url, args=(url,body))
    thread.start()
    #delays.append(rt_delay)
    #print('delay: %d' % (rt_delay))
    rt_delay = int(round((time.time() - start)*1000))

    #esp_console = ser.readlines()

    time.sleep(interval)
    if q.qsize() > 0:
        print(q.queue[-1])

    ts += 1

thread.join()

