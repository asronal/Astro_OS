#!/usr/bin/env python3
# Makes star-rst.csv, the reference that test_rise_set.cpp compares against. It's an
# independent calculation, from PyEphem, for what JPL Horizons can't provide: stars.
#
#   python3 -m venv /tmp/ephem && /tmp/ephem/bin/pip install ephem
#   /tmp/ephem/bin/python make-star-refs.py > star-rst.csv
import sys, math, ephem
stars = [("Sirius","6:45:08.92","-16:42:58.0"),("Vega","18:36:56.34","38:47:01.3"),("Betelgeuse","5:55:10.31","7:24:25.4"),
         ("Polaris","2:31:49.09","89:15:50.8"),("Canopus","6:23:57.11","-52:41:44.4"),("Capella","5:16:41.36","45:59:52.8"),("M31","0:42:44.33","41:16:07.5")]
sites = [("Greenwich",51.4769,-0.0005,46),("Sydney",-33.8688,151.2093,58),("Tromso",69.6492,18.9553,10),("Quito",-0.1807,-78.4678,2850)]
dates = ["2026/11/07 00:00:00","2026/11/07 13:30:00","2027/03/20 06:15:00","2027/06/21 20:45:00"]
print("# PyEphem %s, fixed J2000 position, no proper motion, horizon -34', pressure 0. Days from the start." % ephem.__version__)
print("# star,ra_h,dec_deg,site,lat,lon,elev,start_jd,state,rise_days,transit_days,set_days")
J=2415020.0  # ephem's Dublin Julian Date offset
for name,ra,dec in stars:
    body = ephem.FixedBody(); body._ra=ra; body._dec=dec; body._epoch=ephem.J2000
    for sname,lat,lon,elev in sites:
        for d in dates:
            o = ephem.Observer(); o.lat=str(lat); o.lon=str(lon); o.elevation=elev; o.pressure=0; o.horizon="-0:34"; o.date=d
            start=float(o.date)
            state="rs"; r=t=s=""
            try:
                t="%.6f"%(float(o.next_transit(body))-start)
                try:
                    r="%.6f"%(float(o.next_rising(body))-start); s="%.6f"%(float(o.next_setting(body))-start)
                except ephem.AlwaysUpError: state="up"; r=s=""
                except ephem.NeverUpError: state="never"; r=s=""
            except Exception as e:
                state="skip"
            ra_h=float(ephem.hours(ra))*12/math.pi; dec_d=math.degrees(float(ephem.degrees(dec)))
            print(f"{name},{ra_h:.7f},{dec_d:.6f},{sname},{lat},{lon},{elev},{start+J:.6f},{state},{r},{t},{s}")
