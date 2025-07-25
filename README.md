# YAR (Yet Another Relay)

Executes a set of command lines and provides ways to send and receive 
standard input and output to and from them.  

- support simulated broadcast
- pacing (add's delay between each character sent)
- restarts commands if they exit (both success and failure)
- line buffer broadcast output
- prefixing broadcast output
- dynamically add and remove command lines via a simple monitor interfacee

See usage string for the command usage documentation. Eg.
<pre>
  $ ./yar -h
</pre>
This readme is more about its design and use of ptys.

# Build Notes

Assumes Linux and a working install of `gcc` and `make`, and 
internet connection.

To build the `yar` executable all you should have to do is
<pre>
  $ git clone git@github.com:jappavoo/yar.git
  $ cd yar
  $ make
</pre>

As part of the build the following two external repos will be fetched and built:

- `uthash` : from git@github.com:troydhanson/uthash.git
- `libtlpi`: from https://man7.org/tlpi/code/download/tlpi-241221-dist.tar.gz


# Motivation

Yar is motivated by out experience with IBM project Kittyhawk.  
We found that being able to create a broadcast channel to which communciations
to command oriented processes running on remotes systems was a very powerful
and scalable primative.   Project Kittyhawk used
broadcast capabilities of the Bluegene Collective Network to create a "broadcast"
tty abstraction for which we added driver support for to Linux and u-boot.
This allowed us to efficiently, in parallel, send/scatter a "command" and receive
results  back from hundreds of nodes at once.  This ability effecively 
turns the a commanline interface such as bash into a parallel programming 
environment.  Eg. Assuming that `./wrks` is a tty on a control system
where we interactively can run commands that read and write to it. 
Critically, `./wrks` to connected to broadcast channel on which 
128 "worker" systems are attached (via a local tty) and on which each
worker is running `bash` processes who's standard input, output and error
are directed.  As such all the input and output to these `bash` processes
come from and go to the broadcast channel.  One can then use the `./wrks`
tty on the contol system to do fun things ;-)

<pre>
   $ cat ./wrks > /tmp/wrks.output & pid=$!
   $ echo 'insmod foo; lsmod | grep foo; echo -e "\n@DONE@: $MYNODEID"' > wrks
   $ waitfor '@DONE@' 128 /tmp/wrks.output; kill $pid
</pre>

The above assumes that the bash running on each node has a enviornment 
`MYNODEID` which is set to the systems channel rank (an integer from 0,127) 
that uniquely identifies the node.  Similarly it assumes that a program
that can monitor the contents of a file (/tmp/wrks.output) for the occurance
of a regex (in this case the `@DONE@`) some number of time (128).

Similarly
<pre>
  $ cat ./wrks > /tmp/wrks.output & pid=$!
  $ echo '{ (( MYNODEID > 12 && MYNODED <= 16)) && ip -o -4 addr; } && echo @DONE@' > ./wrks 
  $ waitfor 4 '@DONE@' /tmp/wrks.output; kill $pid
</pre>

Or one can even use the broadcast tty to interactively work with all the nodes
<pre>
  $ socat - ./wrks
</pre>

Yar builds on the above idea but attempts to  generalizes it to creating a 
"channel" represented by a broadcast tty to which an arbitrary set of processes
can be connected to.  The process are specified as shell command lines.
Currently, Yar must serially write and reads data from the processes.  However,
the process are running in parallel so while the time to write data grows
with the number of processes attached to the channel the processes themselves
will proceed in parallel to read and operate on the data they receive and write
data back to the channel.


# Design

<pre>
yar 'cmd0,,,,socat - tcp:x.x.x.x:42' 'cmd1,,,,ssh foo@x.x.x.x' 
  
                              Yar Process
							  fd  file
							   0  ptsA
							   1  ptsA
							   2  ptsA
							   3  ptmB
							   4  ptmC
                               5  ptmZ
							      
	      GBLS.cmdsHash    GBLS.btty.mfd=ptmZ
		    name,cmd_t *   GBLS.btty.sfd=ptsZ  // to keep slave persistent
          "cmd0",&cmd0
		  "cmd1",&cmd1
          
	  cmd0            cmd1    ...
      .pid=43         .pid=44
     .cmdtty.mfd=ptmB .cmdtty.mfd=ptmD
	 .cmdtty.sfd=-1   .cmdtty.sfd=-1
     .clttty.mfd=ptmC .clttty.mfd=ptmE
     .clttty.sfd=-1   .clttty.sfd=-1
	 
        Command Processes                      Client Interfaces
   43           44	                             ptsZ -> ./btty
fd   tty      fd  tty                            ptsC -> ./cmd0
 0 ptsB       0  ptsD                            ptsE -> ./cmd1
 1 ptsB       1  ptsD
 2 ptsB       2  ptsD
</pre>

Must decide what to do about who is the session leader and the controlling tty



# Development notes on tty and pty semantics 

## References

1. A good starting point https://yakout.io/blog/terminal-under-the-hood/
2. Next step includes a nice discussion of job control and signals https://www.linusakesson.net/programming/tty/
3. kernel docs  https://docs.kernel.org/driver-api/tty/tty_buffer.html
3. And of course the kernel source Eg.
	- pty implementation linux-6.12.8/drivers/tty/pty.c
    - core tty data structures linux-6.12.8/include/linux/tty.h
    - default line discipline implementation linux-6.12.8/drivers/tty/n_tty.c


## background 
The pty code uses two static instances of tty_driver objects one for 
the master and one for the slave instances 
(ptm_driver and pts_driver, see pty.c).  The tty struct instances 
for all masters point the ptm_driver global instance and 
similarly all slaves point the global pts_driver instance. (tty.driver)

The pty semantics defined in pty.c are implmented ontop of the underlying
tty semantics and data structures.  Two critical fields of the tty struct
that the pty semantics use are 1) tty.link and 2) tty.count (as well 
as tty.driver)

The pty semantics uses the tty.link field to connect the master and the slave sides.
Eg. pty is composed of two struct tty instances one for the master and one 
for the slave the master link points the slave tty struct and the slave link points 
to the master tty struct.  The pty code use the tty.count field to track the 
number of process that have the master and slave open. 

To fully understand the relationship between the pty and tty code look 
at unix98_pty_init, ptmx_open, and pty_open
### open

Likely begins with tty_open -- 
1. first open
  tty_open_by_driver
     first open then calls tty_init_dev to initialized the device
	      -- Lets assume we go here because of first open of ptm
		       1. allocate a new tty structure (for master instance)
		       2. calls tty_driver_install_tty
			       which will get us to pty_common_install
				   pty_common_install:
				      happens only on master to init master and create 
					  new tty struct and init it as a slave 
					  1. alloc slave tty struct
					  2. point the link fields appropriately
					  3. create 2 8k buffers 
					      1. slave tty port is set to one of them
						  2. master tty port is set to the other
						  3. slave port.itty set set to point to the slave tty


Exmaining pty_open we see:

``` C
	if (tty->driver->subtype == PTY_TYPE_SLAVE && tty->link->count != 1)
		goto out;
```

This implies that an open of a slave tty will exit with EIO if the master
is not open (remember a master can only be opened once)


I belive the above will be invoked by tty_open when it invokes the 
ops->open function pointer (the driver will have set this to pty_open).




### writes

a tty_port is the persistent storage for a tty device.  ports contain a buffer 
that is used by to accumulate data from a hardware device routine such
as a uart IRQ handler.  In the case of a pty the to transfer data on writes to the master to 
port buffer of the slave and writes to the slave to the port buffer of the master
via the `tty_insert_flip_string_and_push(tty->link->port, data, len)`

``` C
static ssize_t pty_write(struct tty_struct *tty, const u8 *buf, size_t c)
{
	struct tty_struct *to = tty->link;

	if (tty->flow.stopped || !c)
		return 0;

	return tty_insert_flip_string_and_push_buffer(to->port, buf, c);
}
```

The above is called via the tty->ops.write pointer which is invoked by the
tty_put_char code when the line disciple is ready to send a character to the 
actual line device (eg. a uart).  In the case of a pty this turns in to
a direct insertion of characters in to the port of the other side.

So in the end we see that 8Kb buffers worth of data are maintained by 
both the master and slave.  After this point we expect blocking writes 
to block and non-blocking to return EAGAIN.


## Notes 

1. no notify events on master
	- 
tty_kopen -> tty_init_dev -> ops->install -> ... -> pty_common_install
