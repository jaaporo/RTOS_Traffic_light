#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <string.h>
#include <stdio.h>
/* Kolmen pisteen tavoite
1p: Sekvenssin vastaanotto sarjaportista
+1p: Lisätty sekvenssin ajastus, sarjaportista voidaan syöttää sekvenssi ja 
aika jonka LEDit palaa esim R,500:Y,1000:G,500
+1p: refaktoroi ohjelma, niin että valotaskeissa ei ole superlooppeja

Koodi yhdistää Koodi 1 + Koodi 2.
 *
 * Koodi 1:
 * - SW0 = pause / jatka
 * - SW1 = punainen LED on/off
 * - SW2 = keltainen LED on/off
 * - SW3 = vihreä LED on/off
 * - SW4 = keltainen vilkkuminen on/off
 *
 * Koodi 2:
 * - UART-sekvenssi
 * - UART-sekvenssi menee FIFO:n kautta dispatcherille
 * - sekvenssissä annetaan myös LEDin palamisaika
 * - dispatcher käyttää Thread API:a
 * - valotaskuissa ei ole while-looppeja
 * - valotasku suorittaa yhden komennon ja päättyy
 *
 * Esimerkki UART-komennosta:
 *
 * R,1000:Y,500:G,1000
 *
 * = punainen 1000 ms
 * = keltainen 500 ms
 * = vihreä 1000 ms
*/
// Thread initializations
#define STACKSIZE 500
#define PRIORITY 5


// Led pin configurations
static const struct gpio_dt_spec red = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

//button configuration
#define BUTTON_0 DT_ALIAS(sw0)
#define BUTTON_1 DT_ALIAS(sw1)
#define BUTTON_2 DT_ALIAS(sw2)
#define BUTTON_3 DT_ALIAS(sw3)
#define BUTTON_4 DT_ALIAS(sw4)

static const struct gpio_dt_spec button_0 = GPIO_DT_SPEC_GET_OR(BUTTON_0, gpios, {0});
static struct gpio_callback button_0_data;

static const struct gpio_dt_spec button_1 = GPIO_DT_SPEC_GET_OR(BUTTON_1, gpios, {0});
static struct gpio_callback button_1_data;

static const struct gpio_dt_spec button_2 = GPIO_DT_SPEC_GET_OR(BUTTON_2, gpios, {0});
static struct gpio_callback button_2_data;

static const struct gpio_dt_spec button_3 = GPIO_DT_SPEC_GET_OR(BUTTON_3, gpios, {0});
static struct gpio_callback button_3_data;

static const struct gpio_dt_spec button_4 = GPIO_DT_SPEC_GET_OR(BUTTON_4, gpios, {0});
static struct gpio_callback button_4_data;

// UART initialization
#define UART_DEVICE_NODE DT_CHOSEN(zephyr_shell_uart)
static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

//dispatcher_fifo on paikka, josta se saa uart komennon
K_FIFO_DEFINE(dispatcher_fifo);

// LED taskien thread runko
K_THREAD_STACK_DEFINE(red_stack, STACKSIZE);
struct k_thread red_thread_data;

K_THREAD_STACK_DEFINE(yellow_stack, STACKSIZE);
struct k_thread yellow_thread_data;

K_THREAD_STACK_DEFINE(green_stack, STACKSIZE);
struct k_thread green_thread_data;

K_THREAD_STACK_DEFINE(yellow_blink_stack, STACKSIZE);
struct k_thread yellow_blink_thread_data;

struct k_thread *current_led_thread = NULL;

void dispatcher_task(void *, void *, void*);
void uart_task(void *, void *, void*);

K_THREAD_DEFINE(dis_thread,STACKSIZE,dispatcher_task,NULL,NULL,NULL,PRIORITY,0,0);
K_THREAD_DEFINE(uart_thread,STACKSIZE,uart_task,NULL,NULL,NULL,PRIORITY,0,0);

void red_led_task(void *, void *, void*);
void yellow_led_task(void *, void *, void*);
void green_led_task(void *, void *, void*);
void yellow_blink_task(void *, void *, void *);
//manual LED control
volatile bool red_manual = false;
volatile bool yellow_manual = false;
volatile bool green_manual = false;

//yellow blinking
volatile bool yellow_blink_thread_started = false;
volatile bool yellow_blinking = false;

volatile bool sequence_paused = false; 

//Function for yellow LED. Yellow = red + green
void set_yellow(bool on){
	gpio_pin_set_dt(&red,on);
	gpio_pin_set_dt(&green,on);
}

// FIFO dispatcher data type
struct led_data_t {
	char color;
	int time;
	
};

struct data_t {
	/*************************
	// Add fifo_reserved below
	*************************/
	void *fifo_reserved;
	char msg[100];
};

//function declarations
int init_led();
int init_button();
int init_uart();

void button_0_handler(const struct device *dev,
		      struct gpio_callback *cb,
		      uint32_t pins);

void button_1_handler(const struct device *dev,
		      struct gpio_callback *cb,
		      uint32_t pins);

void button_2_handler(const struct device *dev,
		      struct gpio_callback *cb,
		      uint32_t pins);

void button_3_handler(const struct device *dev,
		      struct gpio_callback *cb,
		      uint32_t pins);	
			  
void button_4_handler(const struct device *dev,
		      struct gpio_callback *cb,
		      uint32_t pins);	


// Main program
int main(void)
{	
	int ret = init_uart();
	if (ret != 0) {
		printk("UART initialization failed!\n");
		return ret;
	}
	//initialize LEDs
	ret = init_led();
	if (ret < 0){
		return ret;
	}
	//initialize button
	ret = init_button();
	if (ret < 0){
		return ret;
	}
	k_thread_create(
	&yellow_blink_thread_data,
	yellow_blink_stack,
	K_THREAD_STACK_SIZEOF(yellow_blink_stack),
	yellow_blink_task,
	NULL,
	NULL,
	NULL,
	PRIORITY,
	0,
	K_FOREVER
);
	return 0;
}

/********************
 * init UART
 */
int init_uart(void) {
	// UART initialization
	if (!device_is_ready(uart_dev)) {
		return 1;
	} 
	return 0;
}

// Initialize leds
int  init_led() {

	// red Led pin initialization
	int ret = gpio_pin_configure_dt(&red, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		printk("Error: Led configure failed\n");		
		return ret;
	}
	// set led off
	gpio_pin_set_dt(&red,0);

	printk("Led initialized ok\n");
	
	// green Led pin initialization
	int ret_green = gpio_pin_configure_dt(&green, GPIO_OUTPUT_ACTIVE);
	if (ret_green < 0) {
		printk("Error: Led configure failed\n");		
		return ret_green;
	}
	// set led off
	gpio_pin_set_dt(&green,0);

	printk("Led initialized ok\n");

	return 0;

}

//button initialization
int init_button() {

	int ret;
	// button 0
	// check that button is ready
	if (!gpio_is_ready_dt(&button_0)) {
		printk("Error: button 0 is not ready\n");
		return -1;
	}
	//configure button as input
	ret = gpio_pin_configure_dt(&button_0, GPIO_INPUT);
	if (ret != 0) {
		printk("Error: failed to configure pin\n");
		return -1;
	}
	//configure button interrupt
	ret = gpio_pin_interrupt_configure_dt(&button_0, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error: failed to configure interrupt on pin\n");
		return -1;
	}
	//initialize callback
	gpio_init_callback(&button_0_data, button_0_handler, BIT(button_0.pin));
	//add callback
	gpio_add_callback(button_0.port, &button_0_data);
	printk("Set up button 0 ok\n");

	//Button 1
	if (!gpio_is_ready_dt(&button_1)) {
		printk("Error: button 0 is not ready\n");
		return -1;
	}
	//configure button as input
	ret = gpio_pin_configure_dt(&button_1, GPIO_INPUT);
	if (ret != 0) {
		printk("Error: failed to configure pin\n");
		return -1;
	}
	//configure button interrupt
	ret = gpio_pin_interrupt_configure_dt(&button_1, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error: failed to configure interrupt on pin\n");
		return -1;
	}
	//initialize callback
	gpio_init_callback(&button_1_data, button_1_handler, BIT(button_1.pin));
	//add callback
	gpio_add_callback(button_1.port, &button_1_data);

	//button 2
	if (!gpio_is_ready_dt(&button_2)) {
		printk("Error: button 0 is not ready\n");
		return -1;
	}
	//configure button as input
	ret = gpio_pin_configure_dt(&button_2, GPIO_INPUT);
	if (ret != 0) {
		printk("Error: failed to configure pin\n");
		return -1;
	}
	//configure button interrupt
	ret = gpio_pin_interrupt_configure_dt(&button_2, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error: failed to configure interrupt on pin\n");
		return -1;
	}
	//initialize callback
	gpio_init_callback(&button_2_data, button_2_handler, BIT(button_2.pin));
	//add callback
	gpio_add_callback(button_2.port, &button_2_data);

	//button 3
	if (!gpio_is_ready_dt(&button_3)) {
		printk("Error: button 0 is not ready\n");
		return -1;
	}
	//configure button as input
	ret = gpio_pin_configure_dt(&button_3, GPIO_INPUT);
	if (ret != 0) {
		printk("Error: failed to configure pin\n");
		return -1;
	}
	//configure button interrupt
	ret = gpio_pin_interrupt_configure_dt(&button_3, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error: failed to configure interrupt on pin\n");
		return -1;
	}
	//initialize callback
	gpio_init_callback(&button_3_data, button_3_handler, BIT(button_3.pin));
	//add callback
	gpio_add_callback(button_3.port, &button_3_data);

	//button 4
	if (!gpio_is_ready_dt(&button_4)) {
		printk("Error: button 0 is not ready\n");
		return -1;
	}
	//configure button as input
	ret = gpio_pin_configure_dt(&button_4, GPIO_INPUT);
	if (ret != 0) {
		printk("Error: failed to configure pin\n");
		return -1;
	}
	//configure button interrupt
	ret = gpio_pin_interrupt_configure_dt(&button_4, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error: failed to configure interrupt on pin\n");
		return -1;
	}
	//initialize callback
	gpio_init_callback(&button_4_data, button_4_handler, BIT(button_4.pin));
	//add callback
	gpio_add_callback(button_4.port, &button_4_data);
	
	return 0;
}

//button interrupt handler. Button 0 = pause
void button_0_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	printk("Button 0 pressed\n");
/* if we are not in pause: 1. Save current state
2. Change state to pause*/
if (!sequence_paused){
	sequence_paused = true;
	printk("Pause ON \n");
}
/* if we are already in pause:
restore the previous state */
else{
	sequence_paused = false;
	printk("Pause OFF\n");
}
}

// Button 1 = red led 0n / off
void button_1_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	red_manual = !red_manual;
	if (red_manual){
		gpio_pin_set_dt(&red,1);
	}
	else{
		gpio_pin_set_dt(&red,0);
	}
}

//Button 2 = yellow on/off
void button_2_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	
	yellow_manual = !yellow_manual;
	if (yellow_manual){
		//yellow = red + green
		set_yellow(true);
	}
	else{
		set_yellow(false);
	}
}

//Button 3 = green led on/off
void button_3_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	
	green_manual = !green_manual;
	if (green_manual){
		gpio_pin_set_dt(&green,1);
	}
	else{
		gpio_pin_set_dt(&green,0);
	}
}

// Button 4 = yellow blinking
void button_4_handler(const struct device *dev,
		      struct gpio_callback *cb,
		      uint32_t pins)
{
	yellow_blinking = !yellow_blinking;

	if (yellow_blinking) {

		printk("Yellow blinking ON\n");

		gpio_pin_set_dt(&red, 0);
		gpio_pin_set_dt(&green, 0);

		if (!yellow_blink_thread_started) {

			/* Ensimmäinen käynnistys */
			k_thread_start(&yellow_blink_thread_data);
			yellow_blink_thread_started = true;

		} else {

			/* Thread on ollut suspend-tilassa */
			k_thread_resume(&yellow_blink_thread_data);
		}
	}
	else {

		printk("Yellow blinking OFF\n");

		k_thread_suspend(&yellow_blink_thread_data);

		set_yellow(false);
	}
}

//uart task
void uart_task(void *unused1, void *unused2, void *unused3)
{
	// Received character from UART
	char rc=0;
	// Message from UART
	char uart_msg[100];
	memset(uart_msg,0,100);
	int uart_msg_cnt = 0;

	while (true) {
		// Ask UART if data available
		if (uart_poll_in(uart_dev,&rc) == 0) {
			// printk("Received: %c\n",rc);
			// If character is not newline, add to UART message buffer
			if (rc != '\r') {
				uart_msg[uart_msg_cnt] = rc;
				uart_msg_cnt++;
			// Character is newline, copy dispatcher data and put to FIFO buffer
			} else {
				printk("UART msg: %s\n", uart_msg);
                //Fifo puskurin käyttö
				struct data_t *buf = k_malloc(sizeof(struct data_t));
				if (buf == NULL) {
					printk("Memory allocation failed\n");
					continue;
				}
				// Copy UART message to dispatcher data
			
				snprintf(buf->msg, 100, "%s", uart_msg);

				// You need to:
				// Put dispatcher data to FIFO buffer
				k_fifo_put(&dispatcher_fifo, buf);	
				// Clear UART receive buffer
				uart_msg_cnt = 0;
				memset(uart_msg,0,100);

			}
		}
		k_msleep(10);
	}
}
int parse_uart_string(char *input, struct led_data_t *commands_out, int max_size) {
   int count = 0;

   // First tokenisation splits key-value pairs by ':'
   char *pair = strtok(input, ":");

   while (pair != NULL && count < max_size) {
       // Parse the color character and integer directly from the pair
       if (sscanf(pair, "%c,%d", &commands_out[count].color, &commands_out[count].time) == 2) {
           count++;
       }
       pair = strtok(NULL, ":");
   }

   return count; // Returns number of successfully parsed commands
}

/********************
 * Dispatcher task
 */
void dispatcher_task(void *unused1, void *unused2, void *unused3)
{
	struct led_data_t commands[10];
	int num_commands; 
	while (true) {
		struct data_t *rec_item;
		//odotetaan fifoa
		rec_item = k_fifo_get(&dispatcher_fifo, K_FOREVER);
		
		printk("Dispatcher: %s\n", rec_item->msg);
		num_commands = parse_uart_string(rec_item->msg, commands, 10);

		//vapauta uart
		k_free(rec_item);

	for (int i = 0; i < num_commands; i++){
		printk("commands: %c,%d\n", commands[i].color, commands[i].time);

		while(sequence_paused){
			k_msleep(50);
		}

		if (commands[i].color == 'R'){
			printk("punanen alkaa\n");
			
			current_led_thread = &red_thread_data;
			k_thread_create(&red_thread_data, red_stack,K_THREAD_STACK_SIZEOF(red_stack),red_led_task,&commands[i].time,NULL,NULL,PRIORITY,0,K_NO_WAIT);
			
			printk("punanen\n");

			printk("punanen lahetetty\n");
					
			k_thread_join(&red_thread_data, K_FOREVER);
			current_led_thread = NULL;
			printk("punanen vapautettu\n");

		}
		else if (commands[i].color == 'Y'){
			printk("keltanen alkaa\n");

			current_led_thread = &yellow_thread_data;			
			k_thread_create(&yellow_thread_data, yellow_stack, K_THREAD_STACK_SIZEOF(yellow_stack),yellow_led_task,&commands[i].time,NULL,NULL,PRIORITY,0,K_NO_WAIT);
			printk("keltanen\n");

			printk("keltanen lahetetty\n");

			k_thread_join(&yellow_thread_data, K_FOREVER);
			printk("keltanen vapautettu\n");
			current_led_thread = NULL;
		}
		else if (commands[i].color == 'G'){
			printk("vihrea alkaa\n");
			current_led_thread = &green_thread_data;			
			k_thread_create(&green_thread_data, green_stack,K_THREAD_STACK_SIZEOF(green_stack),green_led_task,&commands[i].time,NULL,NULL,PRIORITY,0,K_NO_WAIT);
			printk("vihrea\n");

			printk("vihrea lahetetty\n");
			k_thread_join(&green_thread_data, K_FOREVER);
			printk("vihreä vapautettu\n");
			current_led_thread = NULL;
		}
		else{
			printk("unknown color: \n");
			
			continue;
		}
		
	}	
					
}
}

// Task to handle red led
void red_led_task(void *aika, void *, void*) {
	int *time;
	time =(int*)aika;
	printk("Red led thread started\n");

		printk("RED: %d ms\n",*time);
		
		// 1. set led on 
		gpio_pin_set_dt(&red,1);
		printk("Red on\n");

		k_msleep(*time);

		// 3. set led off
		gpio_pin_set_dt(&red,0);
		printk("Red off\n");
	
	}

// Task to handle yellow led
void yellow_led_task(void *aika, void *, void*) {
	int *time;
	time =(int*)aika;
	printk("yellow led thread started\n");

		printk("yellow:  %d ms\n",*time);
		
		// 1. set led on 
		
		set_yellow(true);
		printk("Yellow on\n");

		k_msleep(*time);

		// 3. set led off
		set_yellow(false);
		printk("Yellow off\n");

	}
void yellow_blink_task(void *, void *, void *)
{
	while (true) {

		set_yellow(true);
		k_msleep(500);

		set_yellow(false);
		k_msleep(500);
	}
}	

//Task to handle green led
void green_led_task(void *aika, void *, void*) {
	int *time;
	time =(int*)aika;
	printk("green led thread started\n");

		printk("green:  %d ms\n",*time);
		
		// 1. set led on 
		gpio_pin_set_dt(&green,1);
		printk("green on\n");

		k_msleep(*time);

		// 3. set led off
		gpio_pin_set_dt(&green,0);
		printk("green off\n");

	}