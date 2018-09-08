How to perform experiment using FMOF in contiki OS
===================================================

To start experiment on FMOF, First, open the terminal on the desktop.
On the terminal window, go to cooja simulator directory by typing

cd contiki/tools/cooja 

Then start cooja with the following command 

ant run 

If you receive any build fail error, stating "unable to delete file in the serial-socket", use

sudo ant run

With this, you will be required to enter password. In the password bar type,

user

Once done, cooja environment will open. In the menu bar, go to 

File-> New simulation

A new simulation box will appear prompting you to give a name for the simulation.
Once done, click on the "Create button" to create a new simlation environment with different bars.

On the simulation window menu bar, click on   

motes-> add motes-> create new mote type-> select sky mote.

Sky mote is the mote type used for FMOF experiment.

In the new window for mote type compilation, you may give your own description then, 
browse for the firmware/contiki process source code.

To browse for the firm ware, go to 

examples-> FMOF-smart-Hop 

There, you will see three source code process folder namely, rpl-udp-client, rpl-udp-forwarder and rpl-udp-server.
Inside it you will find a source code file called udp-client.c. Click open then compile. The same process applies to   
rpl-udp-forwarder and rpl-udp-server.

Upon successful compilation, click create.

This will prompt a new window with informatio to put the number of new motes, then click on 

Add motes

For the client.c and server.c code use 1 new mote for each.
For the forwarder you can put as many as you wish, between 1 and 10.  
It is choose to use as many mobile node as possible, but you would need to update the position file for that. 
For this work, client code is for the Mobile node and we made only node 1 to be mobile in the postion file
located in the FMOF-smart-Hop folder.

to use the position file, go to

Tools-> mobility

In the select position file window, go to 

/home/user/contiki/examples/FMOF-smart-hop

There you will find position files. select 1, example 1m.s.pos

Once done, click on start on the simulation control panel to start the simulation.

In the network panel you can use the "view" menu to select mote type and other preferences.


MODIFIED FILES 
=================
The following are files modified in contiki for the implementation of FMOF.

1. udp-client.c in FMOF-smart-hop-> rpl-udp-client
2. udp-forwarder.c in FMOF-smart-hop-> rpl-udp-forwarder
3. udp-server.c in FMOF-smart-hop-> rpl-udp-server
4. rpl-icmp6.c
5. rpl-private.h
6. rpl-unreachv2.c
