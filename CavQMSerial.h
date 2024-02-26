#ifndef _CAV_QM_SER_H_
#define _CAV_QM_SER_H_

//---------------------------------------------------------------------------
// Global veriable and defination
//---------------------------------------------------------------------------
#define DRIVER_AUTHOR "Anh Duong"
#define DRIVER_DESC "Cavli QM Serial"
#define NUM_BULK_EPS 1
#define MAX_BULK_EPS 6

#define CAV_PORT_NAME_LEN 128
#define CAV_INT_BUF_SIZE 256
#define CAV_ERR_CNT_LIMIT 5
#define CAV_ID_STR "CAVLI"
#define CAV_SER_DTR 0x01
#define CAV_SER_RTS 0x02

// Global pointer to usb_serial_generic_close function
// This function is not exported, which is why we have to use a pointer
// instead of just calling it.
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 26))
void (*gpClose)(struct usb_serial_port *, struct file *);
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 30))
void (*gpClose)(struct tty_struct *, struct usb_serial_port *, struct file *);
#else // > 2.6.30
void (*gpClose)(struct usb_serial_port *);
int (*gpWrite)(struct tty_struct *, struct usb_serial_port *,
	       const unsigned char *, int);
#endif

// DBG macro
#define DBG(format, arg...)                                              \
	if (debug == 1) {                                                \
		printk(KERN_INFO "CavSerial::%s " format, __FUNCTION__, \
		       ##arg);                                           \
	}

#define CAV_DBG(_context_, _dbg_str_) \
	{                              \
		DBG _dbg_str_;         \
	}

#define CavDBG(_context_, _format_, _arg_...)                                               \
	{                                                                                    \
		/*printk( KERN_INFO "CavQMSerial::%s <%s> " _format_,                       \
    __FUNCTION__, CavPort(_context_,NULL), ## _arg_ );*/ \
	}

typedef struct _cav_device_context {
	struct usb_serial *MySerial;
	struct usb_serial_port *MyPort;
	int InterfaceNumber;
	int bInterruptPresent;
	struct urb *pIntUrb; // usb_alloc_urb( 0, GFP_KERNEL );
	int IntPipe;
	int bDevClosed;
	int bDevRemoved;
	char IntBuffer[CAV_INT_BUF_SIZE];
	int IntErrCnt;
	int OpenRefCount;
	spinlock_t AccessLock;
	ulong DebugMask;
	char PortName[CAV_PORT_NAME_LEN];
} cav_device_context;

/*=========================================================================*/
// Function Prototypes
/*=========================================================================*/

// Attach to correct interfaces
static int CavProbe(struct usb_serial *pSerial,
		     const struct usb_device_id *pID);

// Start GPS if GPS port, run usb_serial_generic_open
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 26))
int CavOpen(struct usb_serial_port *pPort, struct file *pFilp);
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 31))
int CavOpen(struct tty_struct *pTTY, struct usb_serial_port *pPort,
	     struct file *pFilp);
#else // > 2.6.31
int CavOpen(struct tty_struct *pTTY, struct usb_serial_port *pPort);
#endif

// Stop GPS if GPS port, run usb_serial_generic_close
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 26))
void CavClose(struct usb_serial_port *, struct file *);
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 30))
void CavClose(struct tty_struct *, struct usb_serial_port *, struct file *);
#else // > 2.6.30
void CavClose(struct usb_serial_port *);
int CavWrite(struct tty_struct *tty, struct usb_serial_port *port,
	      const unsigned char *buf, int count);
int CavAttach(struct usb_serial *serial);
void CavDisconnect(struct usb_serial *serial);
void CavRelease(struct usb_serial *serial);
void IntCallback(struct urb *pIntUrb);
int ResubmitIntURB(struct urb *pIntUrb);
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 25))
// Read data from USB, push to TTY and user space
static void CavReadBulkCallback(struct urb *pURB);
#endif

// Set reset_resume flag
int CavSuspend(struct usb_interface *pIntf, pm_message_t powerEvent);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 23))
// Restart URBs killed during usb_serial_suspend
int CavResume(struct usb_interface *pIntf);
#endif

char *CavPort(cav_device_context *context, struct usb_serial_port *pPort);
void CavSetDtrRts(cav_device_context *context, __u16 DtrRts);
void PrintHex(void *Context, const unsigned char *pBuffer, int BufferSize,
	      char *Tag);

#endif /* _CAV_QM_SER_H_ */
