//---------------------------------------------------------------------------
// Include Files
//---------------------------------------------------------------------------
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
#include <linux/module.h>
#endif
#include "version.h"
#include "CavQMSerial.h"

#define C10QM_VID 0x05C6
#define C10QM_PID 0x9025
#define C10QM_AT_INTF_NUM 2
#define C10QM_GNSS_INTF_NUM 3

// Debug flag
static ulong debug;

static const struct usb_device_id CavConfigVIDPIDTable[] = {
	{ .driver_info = 0xffff },
	// Terminating entry
	{}
};
MODULE_DEVICE_TABLE(usb, CavConfigVIDPIDTable);

/*=========================================================================*/
// Struct usb_serial_driver
// Driver structure we register with the USB core
/*=========================================================================*/
static struct usb_driver CavDriver = {
	.name = "CavSerial",
#if ((LINUX_VERSION_CODE < KERNEL_VERSION(3, 5, 0)))
	.probe = usb_serial_probe,
	.disconnect = usb_serial_disconnect,
#endif
	//.id_table   = CavVIDPIDTable,
	.id_table = CavConfigVIDPIDTable,
	.suspend = CavSuspend,
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 23))
	.resume = CavResume,
#else
	.resume = usb_serial_resume,
#endif
	.supports_autosuspend = true,
};

/*=========================================================================*/
// Struct usb_serial_driver
/*=========================================================================*/
static struct usb_serial_driver gCavDevice = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "CavSerial driver",
	},
	.description = "CavSerial",
	//.id_table            = CavVIDPIDTable,
	.id_table = CavConfigVIDPIDTable,
	.usb_driver = &CavDriver,
	.num_ports = 1,
	.probe = CavProbe,
	.open = CavOpen,
	.attach = CavAttach,
	.disconnect = CavDisconnect,
	.release = CavRelease,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 25))
	.num_interrupt_in = NUM_DONT_CARE,
	.num_bulk_in = 1,
	.num_bulk_out = 1,
	.read_bulk_callback = CavReadBulkCallback,
#endif
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0))
static struct usb_serial_driver *const gCavDevices[] = { &gCavDevice, NULL };
#endif

/*===========================================================================
METHOD:
   CavPort

DESCRIPTION:
   Returns a name assigned to the serial port

PARAMETERS:
   context: [ I ] - private context for the serial device
   pPort:   [ I ] - serial port structure associated with the device

RETURN VALUE:
   NULL-terminated string describing the name assigned to the serial port
===========================================================================*/
char *CavPort(cav_device_context *context, struct usb_serial_port *pPort)
{
	char *id = CAV_ID_STR;

	if (pPort != NULL) {
		if (pPort->port.tty != NULL) {
			id = pPort->port.tty->name;
		}
	} else if (context != NULL) {
		if (context->PortName[0] != 0) {
			id = context->PortName;
		} else if (context->MyPort != NULL) {
			if (context->MyPort->port.tty != NULL) {
				id = context->MyPort->port.tty->name;
			}
		}
	}

	return id;
} // CavPort

void PrintHex(void *Context, const unsigned char *pBuffer, int BufferSize,
	      char *Tag)
{
	char pPrintBuf[896];
	int pos, bufSize;
	int status;
	cav_device_context *context = NULL;

	if (Context != NULL) {
		context = (cav_device_context *)Context;
	}

	memset(pPrintBuf, 0, 896);

	if (BufferSize < 896) {
		bufSize = BufferSize;
	} else {
		bufSize = 890;
	}
	CavDBG(context, "=== %s data %d/%d Bytes ===\n", Tag, bufSize,
		BufferSize);
	for (pos = 0; pos < bufSize; pos++) {
		status = snprintf((pPrintBuf + (pos * 3)), 4, "%02X ",
				  *(u8 *)(pBuffer + pos));
		if (status != 3) {
			CavDBG(context, "snprintf error %d\n", status);
			return;
		}
	}
	CavDBG(context, "   : %s\n", pPrintBuf);
	return;
}

/*===========================================================================
METHOD:
   CavSetDtrRts

DESCRIPTION:
   Set or clear DTR/RTS over the USB control pipe

PARAMETERS:
   context: [ I ] - private context for the serial device
   DtrRts:  [ I ] - DTR/RTS bits

RETURN VALUE:
   none
===========================================================================*/
void CavSetDtrRts(cav_device_context *context, __u16 DtrRts)
{
	int dtrResult;

	if (context->bInterruptPresent == 0) {
		return;
	}

	CavDBG(context, "--> 0x%x\n", DtrRts);
	dtrResult = usb_control_msg(context->MySerial->dev,
				    usb_sndctrlpipe(context->MySerial->dev, 0),
				    0x22, 0x21, DtrRts,
				    context->InterfaceNumber, NULL, 0, 100);
	CAV_DBG(context, ("<%s> <-- Set DTR/RTS 0x%x\n",
			   CavPort(context, NULL), DtrRts));
} // CavSetDtrRts

//---------------------------------------------------------------------------
// USB serial core overridding Methods
//---------------------------------------------------------------------------

static int CavCheckIntf(struct usb_interface *pIface)
{
	struct usb_device *dev;
	unsigned int intfNum = 0;

	dev = interface_to_usbdev(pIface);
	if (!pIface || !dev) {
		DBG("Invalid usb device data\n");
		return -EINVAL;
	}

	DBG("VID: %X,  PID: %X, InterfaceNumber: %X\n", dev->descriptor.idVendor,
		dev->descriptor.idProduct,
		pIface->cur_altsetting->desc.bInterfaceNumber);

	intfNum = pIface->cur_altsetting->desc.bInterfaceNumber;
	if ((dev->descriptor.idVendor != C10QM_VID)
			|| (dev->descriptor.idProduct != C10QM_PID)
			|| ((intfNum != C10QM_AT_INTF_NUM) && (intfNum != C10QM_GNSS_INTF_NUM))
		) {
		DBG("Not C10QM AT/GNSS serial interface\n");
		return -EINVAL;
	}
	return 0;
}

/*===========================================================================
METHOD:
   CavProbe (Free Method)

DESCRIPTION:
   Attach to correct interfaces

PARAMETERS:
   pSerial    [ I ] - Serial structure
   pID        [ I ] - VID PID table

RETURN VALUE:
   int - negative error code on failure
         zero on success
===========================================================================*/
static int CavProbe(struct usb_serial *pSerial,
		     const struct usb_device_id *pID)
{
	// Assume failure
	int nRetval = -ENODEV;
	int nNumInterfaces;
	int nInterfaceNum;
	void *context = usb_get_serial_data(pSerial);
	int interruptOk = 0, pipe = 0, intPipe = 0;

	if (CavCheckIntf(pSerial->interface) != 0) {
		DBG("CavCheckIntf failed\n");
		return -EINVAL;
	}

	DBG("-->CavProbe\n");

	// Test parameters
	if ((pSerial == NULL) || (pSerial->dev == NULL) ||
	    (pSerial->dev->actconfig == NULL) || (pSerial->interface == NULL) ||
	    (pSerial->interface->cur_altsetting == NULL) ||
	    (pSerial->type == NULL)) {
		DBG("<--CavProbe: invalid parameter\n");
		return -EINVAL;
	}

	nNumInterfaces = pSerial->dev->actconfig->desc.bNumInterfaces;
	nInterfaceNum =
		pSerial->interface->cur_altsetting->desc.bInterfaceNumber;
	DBG("Obj / Ctxt = 0x%p / 0x%p\n", pSerial, context);
	DBG("Num Interfaces = %d\n", nNumInterfaces);
	DBG("This Interface = %d\n", nInterfaceNum);
	DBG("Serial private = 0x%p\n", pSerial->private);

	if (nNumInterfaces == 1) {
		DBG("SIngle function device detected\n");
	} else {
		DBG("Composite device detected\n");
	}

	nRetval = usb_set_interface(pSerial->dev, nInterfaceNum, 0);
	if (nRetval < 0) {
		DBG("Could not set interface, error %d\n", nRetval);
	}
	// Check for recursion
	if (pSerial->type->close != CavClose) {
		// Store usb_serial_generic_close in gpClose
		gpClose = pSerial->type->close;
		pSerial->type->close = CavClose;
		DBG("Installed CavClose function: 0x%p/0x%p\n", CavClose,
		    gpClose);
	}

	if (pSerial->type->write != CavWrite) {
		gpWrite = pSerial->type->write;
		pSerial->type->write = CavWrite;
	}

	if (nRetval == 0) {
		// Clearing endpoint halt is a magic handshake that brings
		// the device out of low power (airplane) mode
		// NOTE: FCC verification should be done before this, if required
		struct usb_host_endpoint *pEndpoint;
		int endpointIndex;
		int numEndpoints =
			pSerial->interface->cur_altsetting->desc.bNumEndpoints;

		DBG("numEPs=%d\n", numEndpoints);
		for (endpointIndex = 0; endpointIndex < numEndpoints;
		     endpointIndex++) {
			pipe = 0;
			pEndpoint =
				pSerial->interface->cur_altsetting->endpoint +
				endpointIndex;

			DBG("Examining EP 0x%x\n",
			    pEndpoint->desc.bEndpointAddress);
			if (pEndpoint != NULL) {
				if (usb_endpoint_dir_out(&pEndpoint->desc) ==
				    true) {
					pipe = usb_sndbulkpipe(
						pSerial->dev,
						pEndpoint->desc
							.bEndpointAddress);
					nRetval = usb_clear_halt(pSerial->dev,
								 pipe);
					DBG("usb_clear_halt OUT returned %d\n",
					    nRetval);
				} else if (usb_endpoint_dir_in(
						   &pEndpoint->desc) == true) {
					if (usb_endpoint_xfer_int(
						    &pEndpoint->desc) == true) {
						intPipe = usb_rcvintpipe(
							pSerial->dev,
							pEndpoint->desc.bEndpointAddress &
								USB_ENDPOINT_NUMBER_MASK);
						nRetval = usb_clear_halt(
							pSerial->dev, intPipe);
						DBG("usb_clear_halt INT returned %d\n",
						    nRetval);
						interruptOk = 1;
					} else {
						pipe = usb_rcvbulkpipe(
							pSerial->dev,
							pEndpoint->desc.bEndpointAddress &
								USB_ENDPOINT_NUMBER_MASK);
						nRetval = usb_clear_halt(
							pSerial->dev, pipe);
						DBG("usb_clear_halt IN returned %d\n",
						    nRetval);
					}
				}
			}
		}
	}

	if ((nRetval == 0) && (context == NULL)) {
		cav_device_context *myContext;

		context = kzalloc(sizeof(cav_device_context), GFP_KERNEL);
		if (context != NULL) {
			DBG("CavProbe: Created context 0x%p\n", context);
			myContext = (cav_device_context *)context;
			myContext->InterfaceNumber = nInterfaceNum;
			myContext->bInterruptPresent = interruptOk;
			myContext->IntPipe = intPipe;
			myContext->pIntUrb = NULL;
			myContext->bDevClosed = myContext->bDevRemoved = 0;
			myContext->MySerial = pSerial;
			myContext->MyPort = NULL;
			myContext->IntErrCnt = 0;
			myContext->OpenRefCount = 0;
			myContext->DebugMask = debug = 0;
			spin_lock_init(&myContext->AccessLock);
			memset(myContext->PortName, 0, CAV_PORT_NAME_LEN);
			usb_set_serial_data(pSerial, context);
		}
	}
	DBG("<--CavProbe\n");
	return nRetval;
} // CavProbe

/*===========================================================================
METHOD:
   CavAttach

DESCRIPTION:
   Attach to serial device

PARAMETERS:
   serial    [ I ] - Serial structure

RETURN VALUE:
   int - negative error code on failure
         zero on success
===========================================================================*/
int CavAttach(struct usb_serial *serial)
{
	cav_device_context *context;

	DBG("-->CavAttach\n");
	context = (cav_device_context *)usb_get_serial_data(serial);
	if (context->bInterruptPresent == 0) {
		DBG("<--CavAttach: no action\n");
		return 0;
	}

	context->pIntUrb = usb_alloc_urb(0, GFP_KERNEL);
	if (context->pIntUrb == NULL) {
		DBG("<--CavAttach: Error allocating int urb\n");
		return -ENOMEM;
	}

	DBG("<--CavAttach\n");
	return 0;
} // CavAttach

/*===========================================================================
METHOD:
   CavDisconnect

DESCRIPTION:
   Disconnect from serial device

PARAMETERS:
   serial    [ I ] - Serial structure

RETURN VALUE:
   none
===========================================================================*/
void CavDisconnect(struct usb_serial *serial)
{
	cav_device_context *context =
		(cav_device_context *)usb_get_serial_data(serial);

	CAV_DBG(context, ("<%s> -->\n", CavPort(context, NULL)));
	if (context != NULL) {
		context->bDevRemoved = 1;
		if (context->pIntUrb != NULL) {
			usb_kill_urb(context->pIntUrb);
			usb_free_urb(context->pIntUrb);
			context->pIntUrb = NULL;
		} else {
			CAV_DBG(context, ("<%s> Interrupt URB cleared\n",
					   CavPort(context, NULL)));
		}
	}
	CAV_DBG(context, ("<%s> <--\n", CavPort(context, NULL)));
} // CavDisconnect

/*===========================================================================
METHOD:
   CavRelease

DESCRIPTION:
   Release allocated resources

PARAMETERS:
   serial    [ I ] - Serial structure

RETURN VALUE:
   none
===========================================================================*/
void CavRelease(struct usb_serial *serial)
{
	cav_device_context *context =
		(cav_device_context *)usb_get_serial_data(serial);

	CAV_DBG(context, ("<%s> -->\n", CavPort(context, NULL)));
	if (context != NULL) {
		context->bDevRemoved = 1;
		if (context->pIntUrb != NULL) {
			usb_kill_urb(context->pIntUrb);
			usb_free_urb(context->pIntUrb);
			context->pIntUrb = NULL;
		} else {
			CAV_DBG(context, ("<%s> Interrupt URB cleared\n",
					   CavPort(context, NULL)));
		}
		kfree(context);
		context = NULL;
		usb_set_serial_data(serial, NULL);
	}
	CAV_DBG(context, ("<%s> <--\n", CavPort(context, NULL)));
} // CavRelease

/*===========================================================================
METHOD:
   IntCallback

DESCRIPTION:
   Callback function for interrupt URB

PARAMETERS:
   pIntUrb    [ I ] - URB

RETURN VALUE:
   none
===========================================================================*/
void IntCallback(struct urb *pIntUrb)
{
	cav_device_context *context = (cav_device_context *)pIntUrb->context;

	CavDBG(context, "--> status = %d\n", pIntUrb->status);
	if (pIntUrb->status != 0) {
		// Ignore EOVERFLOW errors
		if (pIntUrb->status != -EOVERFLOW) {
			CavDBG(context, "<-- status = %d\n", pIntUrb->status);
			context->IntErrCnt++;
			return;
		}
	} else {
		context->IntErrCnt = 0;
		DBG("IntCallback: %d bytes\n", pIntUrb->actual_length);
		PrintHex(context, pIntUrb->transfer_buffer,
			 pIntUrb->actual_length, "INT");
	}

	if ((context->bDevClosed == 0) && (context->bDevRemoved == 0)) {
		CavDBG(context, "re-activate interrupt pipe 0x%p\n", pIntUrb);
		ResubmitIntURB(pIntUrb);
	}
	CavDBG(context, "<-- status = %d\n", pIntUrb->status);
} // IntCallback

/*===========================================================================
METHOD:
   ResubmitIntURB

DESCRIPTION:
   Re-submit interrupt URB to receive data over USB interrupt pipe

PARAMETERS:
   pIntUrb    [ I ] - URB

RETURN VALUE:
   int - negative error code on failure
         zero on success
===========================================================================*/
int ResubmitIntURB(struct urb *pIntUrb)
{
	int status;
	int interval;
	cav_device_context *context = NULL;

	// Sanity test
	if ((pIntUrb == NULL) || (pIntUrb->dev == NULL)) {
		CAV_DBG(context, ("<%s> <-- NULL URB or dev\n",
				   CavPort(context, NULL)));
		return -EINVAL;
	}
	context = (cav_device_context *)pIntUrb->context;
	CAV_DBG(context, ("<%s> -->\n", CavPort(context, NULL)));
	if ((context->bDevClosed != 0) || (context->bDevRemoved != 0)) {
		CAV_DBG(context,
			 ("<%s> <-- No action\n", CavPort(context, NULL)));
		return 0;
	}
	if (context->IntErrCnt > CAV_ERR_CNT_LIMIT) {
		CAV_DBG(context,
			 ("<%s> <-- stop due to errors %d\n",
			  CavPort(context, NULL), context->IntErrCnt));
		return 0;
	}

	// Interval needs reset after every URB completion
	interval = 9;

	// Reschedule interrupt URB
	usb_fill_int_urb(pIntUrb, pIntUrb->dev, pIntUrb->pipe,
			 pIntUrb->transfer_buffer,
			 pIntUrb->transfer_buffer_length, pIntUrb->complete,
			 pIntUrb->context, interval);
	status = usb_submit_urb(pIntUrb, GFP_ATOMIC);
	CAV_DBG(context,
		 ("<%s> <-- status %d\n", CavPort(context, NULL), status));

	return status;
} // ResubmitIntURB

/*===========================================================================
METHOD:
   CavOpen (Free Method)

DESCRIPTION:
   Start GPS if GPS port, run usb_serial_generic_open

PARAMETERS:
   pTTY    [ I ] - TTY structure (only on kernels <= 2.6.26)
   pPort   [ I ] - USB serial port structure
   pFilp   [ I ] - File structure (only on kernels <= 2.6.31)

RETURN VALUE:
   int - zero for success
       - negative errno on error
===========================================================================*/
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 26))
int CavOpen(struct usb_serial_port *pPort, struct file *pFilp)
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 31))
int CavOpen(struct tty_struct *pTTY, struct usb_serial_port *pPort,
	     struct file *pFilp)
#else // > 2.6.31
int CavOpen(struct tty_struct *pTTY, struct usb_serial_port *pPort)
#endif
{
	cav_device_context *context = NULL;
	int genericOpenStatus;
	unsigned long flags;
#ifdef GPS_AUTO_START
	const char startMessage[] = "$GPS_START";
	int bytesWrote;
	int nResult;
#endif

	CAV_DBG(NULL, ("<%s> -->\n", CavPort(NULL, pPort)));

	// Test parameters
	if ((pPort == NULL) || (pPort->serial == NULL) ||
	    (pPort->serial->dev == NULL) ||
	    (pPort->serial->interface == NULL) ||
	    (pPort->serial->interface->cur_altsetting == NULL)) {
		CAV_DBG(NULL, ("<%s> <-- invalid parameter\n",
				CavPort(NULL, pPort)));
		return -EINVAL;
	}

	context = (cav_device_context *)usb_get_serial_data(pPort->serial);
	if (context->MyPort == NULL) {
		context->MyPort = pPort;
		if (pPort->port.tty != NULL) {
			strncpy(context->PortName, pPort->port.tty->name,
				(CAV_PORT_NAME_LEN - 1));
		}
	}

	spin_lock_irqsave(&context->AccessLock, flags);
	if (context->OpenRefCount > 0) {
		CavDBG(context, "<--device busy, open denied. RefCnt=%d\n",
			context->OpenRefCount);
		spin_unlock_irqrestore(&context->AccessLock, flags);
		return -EIO;
	} else {
		context->OpenRefCount++;
		spin_unlock_irqrestore(&context->AccessLock, flags);
	}
#ifdef GPS_AUTO_START
	// Is this the GPS port?
	if (pPort->serial->interface->cur_altsetting->desc.bInterfaceNumber ==
	    3) {
		// Send startMessage, 1s timeout
		nResult = usb_bulk_msg(
			pPort->serial->dev,
			usb_sndbulkpipe(pPort->serial->dev,
					pPort->bulk_out_endpointAddress),
			(void *)&startMessage[0], sizeof(startMessage),
			&bytesWrote, 1000);
		if (nResult != 0) {
			CAV_DBG(NULL,
				 ("<%s> <-- error %d sending startMessage\n",
				  CavPort(NULL, pPort), nResult));
			return nResult;
		}
		if (bytesWrote != sizeof(startMessage)) {
			CAV_DBG(NULL, ("<%s> <-- invalid write size %d, %lu\n",
					CavPort(NULL, pPort), bytesWrote,
					sizeof(startMessage)));
			return -EIO;
		}
	}
#endif // GPS_AUTO_START

	context->bDevClosed = 0;
	if ((context->pIntUrb != NULL) && (context->bDevRemoved == 0)) {
		if (context->bInterruptPresent != 0) {
			int interval = 9;

			CAV_DBG(NULL, ("<%s> start interrupt EP\n",
					CavPort(NULL, pPort)));
			context->IntErrCnt = 0;
			usb_fill_int_urb(context->pIntUrb,
					 context->MySerial->dev,
					 context->IntPipe, context->IntBuffer,
					 CAV_INT_BUF_SIZE, IntCallback,
					 context, interval);

			usb_submit_urb(context->pIntUrb, GFP_KERNEL);

			// set DTR/RTS
			CavSetDtrRts(context, (CAV_SER_DTR | CAV_SER_RTS));
		}
	}

	// Pass to usb_serial_generic_open
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 26))
	genericOpenStatus = usb_serial_generic_open(pPort, pFilp);
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 31))
	genericOpenStatus = usb_serial_generic_open(pTTY, pPort, pFilp);
#else // > 2.6.31
	genericOpenStatus = usb_serial_generic_open(pTTY, pPort);
#endif

	if (genericOpenStatus != 0) {
		spin_lock_irqsave(&context->AccessLock, flags);
		context->OpenRefCount--;
		spin_unlock_irqrestore(&context->AccessLock, flags);
	}

	CavDBG(context, "<-- ST %d RefCnt %d\n", genericOpenStatus,
		context->OpenRefCount);
	return genericOpenStatus;
} // CavOpen

/*===========================================================================
METHOD:
   CavClose (Free Method)

DESCRIPTION:
   Stop GPS if GPS port, run usb_serial_generic_close

PARAMETERS:
   pTTY    [ I ] - TTY structure (only if kernel > 2.6.26 and <= 2.6.30)
   pPort   [ I ] - USB serial port structure
   pFilp   [ I ] - File structure (only on kernel <= 2.6.30)
===========================================================================*/
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 26))
void CavClose(struct usb_serial_port *pPort, struct file *pFilp)
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 30))
void CavClose(struct tty_struct *pTTY, struct usb_serial_port *pPort,
	       struct file *pFilp)
#else // > 2.6.30
void CavClose(struct usb_serial_port *pPort)
#endif
{
#ifdef GPS_AUTO_START
	const char stopMessage[] = "$GPS_STOP";
	int nResult;
	int bytesWrote;
#endif
	cav_device_context *context;
	unsigned long flags;

	CAV_DBG(NULL, ("<%s> -->\n", CavPort(NULL, pPort)));

	// Test parameters
	if ((pPort == NULL) || (pPort->serial == NULL) ||
	    (pPort->serial->dev == NULL) ||
	    (pPort->serial->interface == NULL) ||
	    (pPort->serial->interface->cur_altsetting == NULL)) {
		CAV_DBG(NULL, ("<%s> <-- invalid parameter\n",
				CavPort(NULL, pPort)));
		return;
	}

	context = (cav_device_context *)usb_get_serial_data(pPort->serial);
	context->bDevClosed = 1;
	if (context->pIntUrb != NULL) {
		CAV_DBG(NULL, ("<%s> cancel interrupt URB 0x%p\n",
				CavPort(NULL, pPort), context->pIntUrb));
		usb_kill_urb(context->pIntUrb);
		// clear DTR/RTS
		CavSetDtrRts(context, 0);
	}

	spin_lock_irqsave(&context->AccessLock, flags);
	context->OpenRefCount--;
	spin_unlock_irqrestore(&context->AccessLock, flags);

#ifdef GPS_AUTO_START
	// Is this the GPS port?
	if (pPort->serial->interface->cur_altsetting->desc.bInterfaceNumber ==
	    3) {
		// Send stopMessage, 1s timeout
		nResult = usb_bulk_msg(
			pPort->serial->dev,
			usb_sndbulkpipe(pPort->serial->dev,
					pPort->bulk_out_endpointAddress),
			(void *)&stopMessage[0], sizeof(stopMessage),
			&bytesWrote, 1000);
		if (nResult != 0) {
			CAV_DBG(NULL, ("<%s> error %d sending stopMessage\n",
					CavPort(NULL, pPort), nResult));
		}
		if (bytesWrote != sizeof(stopMessage)) {
			CAV_DBG(NULL, ("<%s> invalid write size %d, %lu\n",
					CavPort(NULL, pPort), bytesWrote,
					sizeof(stopMessage)));
		}
	}
#endif // GPS_AUTO_START

	// Pass to usb_serial_generic_close
	if (gpClose == NULL) {
		CAV_DBG(NULL,
			 ("<%s> <-- NULL gpClose\n", CavPort(NULL, pPort)));
		return;
	}

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 26))
	gpClose(pPort, pFilp);
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 30))
	gpClose(pTTY, pPort, pFilp);
#else // > 2.6.30
	gpClose(pPort);
#endif
	CavDBG(context, "<-- with gpClose RefCnt %d\n", context->OpenRefCount);
} // CavClose

/*===========================================================================
METHOD:
   CavWrite

DESCRIPTION:
   Write data over the USB BULK pipe

PARAMETERS:
   tty:    [ I ] - TTY structure associated with the serial device
   pPort:  [ I ] - the serial port structure
   buf:    [ I ] - buffer containing the USB bulk OUT data
   count:  [ I ] - number of bytes of the USB bulk OUT data

RETURN VALUE:
===========================================================================*/
int CavWrite(struct tty_struct *tty, struct usb_serial_port *pPort,
	      const unsigned char *buf, int count)
{
	void *context = usb_get_serial_data(pPort->serial);

	/***
  if (context != NULL)
  {
     CAV_DBG(NULL, ("<%s> serial ctxt 0x%p / 0x%p\n", CavPort(NULL, pPort),
  pPort->serial, context));
  }
  else
  {
     CAV_DBG(NULL, ("<%s> serial ctxt 0x%p / NULL\n", CavPort(NULL, pPort),
  pPort->serial));
  }
  ***/
	PrintHex(context, buf, count, "SEND");
	return gpWrite(tty, pPort, buf, count);
} // CavWrite

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 25))

/*===========================================================================
METHOD:
   CavReadBulkCallback (Free Method)

DESCRIPTION:
   Read data from USB, push to TTY and user space

PARAMETERS:
   pURB  [ I ] - USB Request Block (urb) that called us

RETURN VALUE:
===========================================================================*/
static void CavReadBulkCallback(struct urb *pURB)
{
	struct usb_serial_port *pPort = pURB->context;
	struct tty_struct *pTTY = pPort->tty;
	int nResult;
	int nRoom = 0;
	unsigned int pipeEP;

	DBG("port %d\n", pPort->number);

	if (pURB->status != 0) {
		DBG("nonzero read bulk status received: %d\n", pURB->status);

		return;
	}

	usb_serial_debug_data(debug, &pPort->dev, __FUNCTION__,
			      pURB->actual_length, pURB->transfer_buffer);

	// We do no port throttling

	// Push data to tty layer and user space read function
	if (pTTY != 0 && pURB->actual_length) {
		nRoom = tty_buffer_request_room(pTTY, pURB->actual_length);
		DBG("room size %d %d\n", nRoom, 512);
		if (nRoom != 0) {
			tty_insert_flip_string(pTTY, pURB->transfer_buffer,
					       nRoom);
			tty_flip_buffer_push(pTTY);
		}
	}

	pipeEP = usb_rcvbulkpipe(pPort->serial->dev,
				 pPort->bulk_in_endpointAddress);

	// For continuous reading
	usb_fill_bulk_urb(pPort->read_urb, pPort->serial->dev, pipeEP,
			  pPort->read_urb->transfer_buffer,
			  pPort->read_urb->transfer_buffer_length,
			  CavReadBulkCallback, pPort);

	nResult = usb_submit_urb(pPort->read_urb, GFP_ATOMIC);
	if (nResult != 0) {
		DBG("failed resubmitting read urb, error %d\n", nResult);
	}
}

#endif

/*===========================================================================
METHOD:
   CavSuspend (Public Method)

DESCRIPTION:
   Set reset_resume flag

PARAMETERS
   pIntf          [ I ] - Pointer to interface
   powerEvent     [ I ] - Power management event

RETURN VALUE:
   int - 0 for success
         negative errno for failure
===========================================================================*/
int CavSuspend(struct usb_interface *pIntf, pm_message_t powerEvent)
{
	struct usb_serial *pDev;

	if (pIntf == 0) {
		return -ENOMEM;
	}

	pDev = usb_get_intfdata(pIntf);
	if (pDev == NULL) {
		return -ENXIO;
	}

	// Unless this is PM_EVENT_SUSPEND, make sure device gets rescanned
	if ((powerEvent.event & PM_EVENT_SUSPEND) == 0) {
		pDev->dev->reset_resume = 1;
	}

	// Run usb_serial's suspend function
	return usb_serial_suspend(pIntf, powerEvent);
} // CavSuspend

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 23))

/*===========================================================================
METHOD:
   CavResume (Free Method)

DESCRIPTION:
   Restart URBs killed during usb_serial_suspend

   Fixes 2 bugs in 2.6.23 kernel
      1. pSerial->type->resume was NULL and unchecked, caused crash.
      2. set_to_generic_if_null was not run for resume.

PARAMETERS:
   pIntf  [ I ] - Pointer to interface

RETURN VALUE:
   int - 0 for success
         negative errno for failure
===========================================================================*/
int CavResume(struct usb_interface *pIntf)
{
	struct usb_serial *pSerial = usb_get_intfdata(pIntf);
	struct usb_serial_port *pPort;
	int portIndex, errors, nResult;

	if (pSerial == NULL) {
		DBG("no pSerial\n");
		return -ENOMEM;
	}
	if (pSerial->type == NULL) {
		DBG("no pSerial->type\n");
		return ENOMEM;
	}
	if (pSerial->type->resume == NULL) {
		// Expected behaviour in 2.6.23, in later kernels this was handled
		// by the usb-serial driver and usb_serial_generic_resume
		errors = 0;
		for (portIndex = 0; portIndex < pSerial->num_ports;
		     portIndex++) {
			pPort = pSerial->port[portIndex];
			if (pPort->open_count > 0 && pPort->read_urb != NULL) {
				nResult = usb_submit_urb(pPort->read_urb,
							 GFP_NOIO);
				if (nResult < 0) {
					// Return first error we see
					DBG("error %d\n", nResult);
					return nResult;
				}
			}
		}

		// Success
		return 0;
	}

	// Execution would only reach this point if user has
	// patched version of usb-serial driver.
	return usb_serial_resume(pIntf);
} // CavResume

#endif

/*===========================================================================
METHOD:
   CavInit (Free Method)

DESCRIPTION:
   Register the driver and device

PARAMETERS:

RETURN VALUE:
   int - negative error code on failure
         zero on success
===========================================================================*/
static int __init CavInit(void)
{
	int nRetval = 0;
	gpClose = NULL;

	gCavDevice.num_ports = NUM_BULK_EPS;

	// Registering driver to USB serial core layer
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0))
	nRetval = usb_serial_register(&gCavDevice);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
	// nRetval = usb_serial_register_drivers( gCavDevices, "Cavli",
	// CavVIDPIDTable);
	nRetval = usb_serial_register_drivers(gCavDevices, "Cavli",
					      CavConfigVIDPIDTable);
#else
	nRetval = usb_serial_register_drivers(&CavDriver, gCavDevices);
#endif

	if (nRetval != 0) {
		return nRetval;
	}

	// Registering driver to USB core layer
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0))
	nRetval = usb_register(&CavDriver);
	if (nRetval != 0) {
		usb_serial_deregister(&gCavDevice);
		return nRetval;
	}
#endif

	// This will be shown whenever driver is loaded
	printk(KERN_INFO "%s: %s\n", DRIVER_DESC, DRIVER_VERSION);

	return nRetval;
} // CavInit

/*===========================================================================
METHOD:
   CavExit (Free Method)

DESCRIPTION:
   Deregister the driver and device

PARAMETERS:

RETURN VALUE:
===========================================================================*/
static void __exit CavExit(void)
{
	gpClose = NULL;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0))
	usb_deregister(&CavDriver);
	usb_serial_deregister(&gCavDevice);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
	usb_serial_deregister_drivers(gCavDevices);
#else
	usb_serial_deregister_drivers(&CavDriver, gCavDevices);
#endif
} // CavExit

// Calling kernel module to init our driver
module_init(CavInit);
module_exit(CavExit);

MODULE_VERSION(DRIVER_VERSION);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("Dual BSD/GPL");

module_param(debug, ulong, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
