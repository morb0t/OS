#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "fsl_pint.h"
#include "fsl_inputmux.h"
#include "fsl_usart.h"
#include "fsl_clock.h"

#include "vfs.h"
#include "target.h"

/***************************************************************************
 * external symbols
 ***************************************************************************/
extern FileObject * opened_fds[];	/* device/vfs.c  */

/***************************************************************************
 * test device driver
 ***************************************************************************/
static int dev_init_test(Device *dev);
static int dev_open_test(FileObject *f);
static int dev_close_test(FileObject *f);
static int dev_read_test(FileObject *f, void *buf, size_t len);

Device dev_test={
    .name="test",
    .refcnt=0,
    .sem_read=NULL,
    .sem_write=NULL,
    .init=dev_init_test,
    .open=dev_open_test,
    .close=dev_close_test,
    .read=dev_read_test,
    .write=NULL,
    .ioctl=NULL
};

static int dev_init_test(Device *dev)
{
    dev->mutex=sem_new(1);
    if (dev->mutex) return 1;
    return 0;
}

static int dev_open_test(FileObject *f)
{
	sem_p(f->dev->mutex);
    if (f->flags & (O_READ)) {
        f->dev->refcnt++;
        sem_v(f->dev->mutex);
        return 1;
    }
    sem_v(f->dev->mutex);
    return 0;
}

static int dev_close_test(FileObject *f)
{
	sem_p(f->dev->mutex);
    f->dev->refcnt--;
    sem_v(f->dev->mutex);
    return 1;
}

static int dev_read_test(FileObject *f, void *buf, size_t len)
{
	int n;
	char *file="ceci est un test\r\n";
	sem_p(f->dev->mutex);
	// calculate max available readable data length
	n=f->offset<strlen(file) ? strlen(file)-f->offset : 0;
	// actually, we have to read the min from available length and expected length
	n=n<(int)len ? n : (int)len;
	memcpy(buf, file+f->offset, n);
	f->offset+=n;
	sem_v(f->dev->mutex);
	return n;
}

/***************************************************************************
 * leds device driver
 ***************************************************************************/
static void leds(uint32_t val)
{
	// bit 0 of val controls LED RED
	GPIO_PinWrite(BOARD_LED_RED_GPIO,BOARD_LED_RED_GPIO_PORT,BOARD_LED_RED_GPIO_PIN, (~(val>>0))&1);
	//  bit 1 of val controls LED GREEN
	GPIO_PinWrite(BOARD_LED_GREEN_GPIO,BOARD_LED_GREEN_GPIO_PORT,BOARD_LED_GREEN_GPIO_PIN, (~(val>>1))&1);
	//  bit 2 of val controls LED BLUE
	GPIO_PinWrite(BOARD_LED_BLUE_GPIO,BOARD_LED_BLUE_GPIO_PORT,BOARD_LED_BLUE_GPIO_PIN, (~(val>>2))&1);
}

static int dev_init_leds(Device *dev);
static int dev_open_leds(FileObject *f);
static int dev_close_leds(FileObject *f);
static int dev_write_leds(FileObject *f, void *buf, size_t len);

Device dev_leds={
    .name="leds",
    .refcnt=0,
	.sem_read=NULL,
	.sem_write=NULL,
    .init=dev_init_leds,
    .open=dev_open_leds,
    .close=dev_close_leds,
	.read=NULL,
	.write=dev_write_leds,
    .ioctl=NULL
};

static int dev_init_leds(Device *dev)
{
	// leds init
	gpio_pin_config_t ledcfg = { kGPIO_DigitalOutput, 1};
	GPIO_PortInit(BOARD_LED_RED_GPIO,BOARD_LED_RED_GPIO_PORT);
	GPIO_PinInit(BOARD_LED_RED_GPIO,BOARD_LED_RED_GPIO_PORT,BOARD_LED_RED_GPIO_PIN,&ledcfg);
	GPIO_PinInit(BOARD_LED_GREEN_GPIO,BOARD_LED_GREEN_GPIO_PORT,BOARD_LED_GREEN_GPIO_PIN,&ledcfg);
	GPIO_PinInit(BOARD_LED_BLUE_GPIO,BOARD_LED_BLUE_GPIO_PORT,BOARD_LED_BLUE_GPIO_PIN,&ledcfg);

	leds(0);



	return 1;
}

static int dev_open_leds(FileObject *f)
{
	sem_p(f->dev->mutex);
    if (f->dev->refcnt || (f->flags & (O_READ|O_NONBLOCK|O_APPEND|O_SHLOCK|O_EXLOCK|O_ASYNC|O_SYNC|O_CREAT|O_TRUNC|O_EXCL)))
        goto err;
    if (f->flags & O_WRITE) {
        f->dev->refcnt++;
        sem_v(f->dev->mutex);
        return 1;
    }
err:
	sem_v(f->dev->mutex);
    return 0;
}

static int dev_close_leds(FileObject *f)
{
	sem_p(f->dev->mutex);
    f->dev->refcnt--;
    sem_v(f->dev->mutex);
    return 1;
}

static int dev_write_leds(FileObject *f, void *buf, size_t len)
{

	sem_p(f->dev->mutex);
	leds(*(uint32_t*)buf);
	sem_v(f->dev->mutex);

    return 1;
}

/***************************************************************************
 * SWCenter External Interrupt Button device driver
 ***************************************************************************/
static int dev_init_btn(Device *dev);
static int dev_open_btn(FileObject *f);
static int dev_close_btn(FileObject *f);
static int dev_read_btn(FileObject *f, void *buf, size_t len);

Device dev_swuser={
    .name="swuser",
    .refcnt=0,
	.sem_read=NULL,
	.sem_write=NULL,
	.init=dev_init_btn,
	.open=dev_open_btn,
	.close=dev_close_btn,
	.read=dev_read_btn,
	.write=NULL,
	.ioctl=NULL
};

/*
 *  ISR callback (IRQ mode !!)
 */
static void on_swuser_cb(pint_pin_int_t pintr, uint32_t pmatch_status)
{
	sem_v(dev_swuser.sem_read);
}

static int dev_init_btn(Device *dev)
{
    /* Connect trigger sources to PINT */
    INPUTMUX_Init(INPUTMUX);
    INPUTMUX_AttachSignal(INPUTMUX, kPINT_PinInt0, kINPUTMUX_GpioPort1Pin9ToPintsel);
    /* Turnoff clock to inputmux to save power. Clock is only needed to make changes */
    INPUTMUX_Deinit(INPUTMUX);
    /* Initialize PINT */
    PINT_Init(PINT);
    /* Setup Pin Interrupt 0 for rising edge */
    PINT_PinInterruptConfig(PINT, kPINT_PinInt0, kPINT_PinIntEnableRiseEdge, on_swuser_cb);
    NVIC_SetPriority(PIN_INT0_IRQn,10);
	/* Enable callbacks for PINT0 by Index */
    PINT_EnableCallbackByIndex(PINT, kPINT_PinInt0);

    dev->sem_read = sem_new(0);
	dev->mutex = sem_new(1);

    return 1;
}

static int dev_open_btn(FileObject *f)
{
	sem_p(f->dev->mutex);
    if (f->dev->refcnt || (f->flags & (O_WRITE|O_NONBLOCK|O_APPEND|O_SHLOCK|O_EXLOCK|O_ASYNC|O_SYNC|O_CREAT|O_TRUNC|O_EXCL)))
        goto err;
    if (f->flags & O_READ) {
        f->dev->refcnt++;
		sem_v(f->dev->mutex);
        return 1;
    }
err:
	sem_v(f->dev->mutex);
    return 0;
}

static int dev_close_btn(FileObject *f)
{
	sem_p(f->dev->mutex);
    f->dev->refcnt--;
	sem_v(f->dev->mutex);
    return 1;
}

static int dev_read_btn(FileObject *f, void *buf, size_t len)
{
	sem_p(f->dev->sem_read);
    return 4;
}

/***************************************************************************
 * UART device driver
 ***************************************************************************/
#define UART_INSTANCE 	USART0
#define BAUDRATE		115200


static int dev_init_uart(Device *dev);
static int dev_open_uart(FileObject *f);
static int dev_close_uart(FileObject *f);
static int dev_read_uart(FileObject *f, void *buf, size_t len);
static int dev_write_uart(FileObject *f, void *buf, size_t len);

Device dev_uart={
    .name="uart",
    .refcnt=0,
	.sem_read=NULL,
	.sem_write=NULL,
	.init=dev_init_uart,
	.open=dev_open_uart,
	.close=dev_close_uart,
	.read=dev_read_uart,
	.write=dev_write_uart,
	.ioctl=NULL
};

static int dev_init_uart(Device *dev){


	usart_config_t config;
	USART_GetDefaultConfig(&config);

	config.baudRate_Bps = BAUDRATE;
	config.enableTx = true;
	config.enableRx = true;

	USART_Init(UART_INSTANCE, &config, CLOCK_GetFreq(kCLOCK_Fro12M));


    dev->mutex=sem_new(1);
    if (dev->mutex) return 1;
    return 0;
}
static int dev_open_uart(FileObject *f){
	sem_p(f->dev->mutex);
	if ((f->flags & O_READ) || (f->flags & O_WRITE)){
		f->dev->refcnt++;
	    sem_v(f->dev->mutex);
	    return 1;
	}
	sem_v(f->dev->mutex);
	return 0;
}
static int dev_close_uart(FileObject *f){
	sem_p(f->dev->mutex);
	f->dev->refcnt--;
	sem_v(f->dev->mutex);
	return 1;
}
static int dev_read_uart(FileObject *f, void *buf, size_t len){
	sem_p(f->dev->mutex);

	USART_ReadBlocking(UART_INSTANCE, (uint8_t*)buf, len);

	sem_v(f->dev->mutex);
	return len;
}
static int dev_write_uart(FileObject *f, void *buf, size_t len){
	sem_p(f->dev->mutex);

	USART_WriteBlocking(UART_INSTANCE, (uint8_t*)buf, len);

	sem_v(f->dev->mutex);

    return len;
}
/***************************************************************************
 * Device table
 ***************************************************************************/
Device * device_table[]={
	&dev_test,
	&dev_leds,
    &dev_swuser,
	&dev_uart,
	NULL
};

/*****************************************************************************
 * Target Init
 *****************************************************************************/
extern Semaphore* vfs_mutex;

void dev_init()
{
    int i=0;
    Device *dev=device_table[0];
    while (dev) {
        if (dev->init) dev->init(dev);
        dev=device_table[++i];
    }
    
    for (int i=0;i<MAX_OPENED_FDS;i++)
        opened_fds[i]=NULL;

    vfs_mutex=sem_new(1);
}

