Allan Deviation gets us

1. Angle Random Walk

2. Bias Instability

Power Spectral Density is applied to a time-dependent graph. It is closely
related to Fourier Transform, in that at frequencies that are a component of the
signal, the PSD graph spikes.

However, it is not identical to PSD. It tells you the _power_ of the signal per
hertz.

The y-axis of a PSD is the unit of the signal squared divded by hertz. The
x-axis is hertz. So, for gyro, the y-axis has units of degrees squared / seconds
squared / hertz.

# General Signal Terminology

Bandwidth is the range of frequencies where there is actually significant power.
By filtering, we can reduce the bandwidth (which is good)

The noise power is the amount of power from the noise over the bandwidth.

# White Noise

White noise consists of constant power density for all frequencies.

So its PSD looks flat, at a constant power density throughout.

One of the best things about white noise is that it is very easy to model.

The noise power for white noise is the power density times the bandwidth.
Observe the units: power density is deg^2/sec^2/Hz, bandwidth difference in two
Hz, which is in Hz. So multiplying get us deg^2/sec^2.

It turns out this noise power is actually the _variance_ for a Gaussian
distribution. If we just randomly sample from the Gaussian distribution, we will
get white noise. Very easy to model.

# Angle Random Walk and Modeling White Noise

Angle Random Walk (ARW) tells us how much the integrated angle will wander over
time as a result of _white rate noise only_. ARW is measured in degrees/√hour
(or degrees/√second). Multiplying ARW by the square root of the time elapsed
since integration started gives the standard deviation of a zero-mean Gaussian
distribution centered on the true angle.
