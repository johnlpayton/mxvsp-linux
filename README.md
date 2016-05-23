# mxvsp-linux

This is the Linux implementation of a host server for mxvsp (a custom protocol) but limited to a single attached
serial port.

mxvsp is a concise protocol primarily to attach to a remote development board using a serial port.
It provides the GUI and the routing engine for a server connected to a development station
over it's serial interface.  Whe running, a remote client can also connect to the same port for development
or monitoring purposes.

The routing is source based.  All data flows to the serail port (development station).  All data
input to the serial port (development station) is broadcasted to all attached TCP/IP connections.  It
is designed to allow development of software from multiple terminals.  It is great for using
a laptop and a destop at the same time.

