#!/usr/bin/env python3

import numpy as np
from matplotlib import pyplot as plt
from scipy import stats

# All values in seconds
net_delay_mean = 0.2
net_delay_stdev = 0.1
net_delay_min = 0.03
net_delay_max = 0.9
bias = 3
# Drift between clocks, in millisconds per second
drift = .0001

step = 3
n = 1000

clock1 = np.arange(0, step*n, step)
n = len(clock1)

jitter_out = np.clip(np.random.randn(n) * net_delay_stdev + net_delay_mean, net_delay_min, net_delay_max)
clock2 = clock1 + bias + jitter_out + np.cumsum(np.tile(drift, n))

jitter_in = np.clip(np.random.randn(n) * net_delay_stdev + net_delay_mean, net_delay_min, net_delay_max)
rt_time = (clock2 - clock1) - bias + jitter_in

slope,intercept,r,p,std_err = stats.linregress(clock1, clock2)

net_delay_est = (rt_time.mean() - rt_time.min() / 2) / 2
# net delay is over-estimated due to the clipped lower bound


bias_est = intercept - net_delay_est


plt.ion()
#plt.scatter(clock1, clock2)

plt.plot(clock2 - clock1)



