# data processing folder

contains python files for running Allan deviation and also visualization.

first, you need python; amything above python 3.10 should work, and i've for
sure tested it on python 3.14.9.

you also need the python dependencies. you can do
`pip install -r requirements.txt` or
[make a python environment](https://realpython.com/python-virtual-environments-a-primer/)
and install the python dependencies there

# visualization

takes the raw data from the flight computer and just plots a bunch of graphs
over time

just looking at the graphs, opens a new window:

```bash
python visualization.py data/DATA.CSV
```

saving the images to a folder:

```bash
python visualization.py data/DATA.CSV --save FOLDER
```

# allan deviation

runs allan deviation

unfortunantely no graph saving feature right now, use the save button at the top
left or screenshot (lmao)

```bash
python allan_deviation.py data/DATA.CSV
```
