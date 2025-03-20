# YAR (Yet Another Relay)

Executes a set of commands and provides ways to send and receive 
standard input and output to and from them.  

- support simulated broadcast
- pacing (add's delay between each character sent)
- restarts commands if they exit


# Design

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

Need to explicity close all other 
fds and decide what to do about
who is the session leader
and the controlling tty



# tty and pty semantics 

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
