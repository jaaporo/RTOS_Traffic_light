#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/timing/timing.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
/* Kolmen pisteen tavoite
1p: lisää ajoitukset liikennevaloihin
+1p: Debug taski
+1p: lisätty assertit*/

// Thread initializations
#define STACKSIZE 500
#define PRIORITY 5


// Led pin configurations
static const struct gpio_dt_spec red = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

//button configurations
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

//dispatcher_fifo is used to pass uart commands from the uart task to the dispatcher task
K_FIFO_DEFINE(dispatcher_fifo);

//debug_fifo is used to pass debug messages from other task to the debug task
K_FIFO_DEFINE(debug_fifo);

#define DEBUG_STACKSIZE 1000
//in zephyr, a smaller number means a higher priority. therefore, debug task has a lower priority than the normal 
#define DEBUG_PRIORITY 6

void debug_task(void *, void *, void *);

//debug task thread stacks and thread data
K_THREAD_STACK_DEFINE(debug_stack, DEBUG_STACKSIZE);
struct k_thread debug_thread_data;

K_THREAD_STACK_DEFINE(red_stack, STACKSIZE);
struct k_thread red_thread_data;

K_THREAD_STACK_DEFINE(yellow_stack, STACKSIZE);
struct k_thread yellow_thread_data;

K_THREAD_STACK_DEFINE(green_stack, STACKSIZE);
struct k_thread green_thread_data;

K_THREAD_STACK_DEFINE(yellow_blink_stack, STACKSIZE);
struct k_thread yellow_blink_thread_data;

//pointer to the currently running LED thread
struct k_thread *current_led_thread = NULL;

void dispatcher_task(void *, void *, void*);
void uart_task(void *, void *, void*);

K_THREAD_DEFINE(dis_thread,STACKSIZE,dispatcher_task,NULL,NULL,NULL,PRIORITY,0,0);
K_THREAD_DEFINE(uart_thread,STACKSIZE,uart_task,NULL,NULL,NULL,PRIORITY,0,0);

void red_led_task(void *, void *, void*);
void yellow_led_task(void *, void *, void*);
void green_led_task(void *, void *, void*);
void yellow_blink_task(void *, void *, void *);

//manual LED control flags
volatile bool red_manual = false;
volatile bool yellow_manual = false;
volatile bool green_manual = false;

//yellow blinking control
volatile bool yellow_blink_thread_started = false;
volatile bool yellow_blinking = false;

//indicates whether the traffic light sequence is paused
volatile bool sequence_paused = false; 

uint64_t sequence_total_us = 0;

//Function for yellow LED. Yellow = red + green
void set_yellow(bool on){
	gpio_pin_set_dt(&red,on);
	gpio_pin_set_dt(&green,on);
}

// data structure used for dispatcher fifo messages
struct led_data_t {
	char color;
	int time;
	
};

// data structure used for debug and dispatcher fifo messages
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

/* Debug logging function
instead of printing directly with printk(), tasks create a debug message 
and place it into the degub fifo. */	  
void debug_log(const char *format, ...){
	struct data_t *buf = k_malloc(sizeof(struct data_t));
	if(buf == NULL){
		return;
	}	
	va_list args;
	va_start(args, format);
	vsnprintf(buf->msg, sizeof(buf->msg), format, args);
	va_end(args);
	k_fifo_put(&debug_fifo, buf);
}

/* Debug task
this is the only task that uses printk().
other tasks send their debug messages through debug_fifo. */
void debug_task(void *unused1, void *unused2, void *unused3){
	while(true){
		struct data_t *rec_item;
		
		rec_item = k_fifo_get(&debug_fifo, K_FOREVER);
		
		printk("%s", rec_item->msg);
		
		k_free(rec_item);
	}
}

// Main program
int main(void)
{	//initialize the Zephyr timing subsystem
	timing_init();
	
	// start the debug task
	k_thread_create(
    &debug_thread_data,
    debug_stack,
    K_THREAD_STACK_SIZEOF(debug_stack),
    debug_task,
    NULL,
    NULL,
    NULL,
    DEBUG_PRIORITY,
    0,
    K_NO_WAIT
);
//initialize UART
	int ret = init_uart();
	if (ret != 0) {
		debug_log("UART initialization failed!\n");
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

	// create the yellow blinking thread.
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
	// check whether the uart device is ready
	if (!device_is_ready(uart_dev)) {
		return 1;
	} 
	return 0;
}

// Initialize leds
int  init_led() {

	// configure the red led as an output
	int ret = gpio_pin_configure_dt(&red, GPIO_OUTPUT_ACTIVE);

	if (ret < 0) {
		debug_log("Error: Led configure failed\n");		
		return ret;
	}

	// set led off
	gpio_pin_set_dt(&red,0);
	debug_log("Led initialized ok\n");
	
	// configure the green led as an output
	int ret_green = gpio_pin_configure_dt(&green, GPIO_OUTPUT_ACTIVE);

	if (ret_green < 0) {
		debug_log("Error: Led configure failed\n");		
		return ret_green;
	}

	// set led off
	gpio_pin_set_dt(&green,0);
	debug_log("Led initialized ok\n");

	return 0;
}

//button initialization
int init_button() {

	int ret;
	// button 0
	// check that button is ready
	if (!gpio_is_ready_dt(&button_0)) {
		debug_log("Error: button 0 is not ready\n");
		return -1;
	}
	//configure button as input
	ret = gpio_pin_configure_dt(&button_0, GPIO_INPUT);
	if (ret != 0) {
		debug_log("Error: failed to configure pin\n");
		return -1;
	}
	//configure button interrupt
	ret = gpio_pin_interrupt_configure_dt(&button_0, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		debug_log("Error: failed to configure interrupt on pin\n");
		return -1;
	}
	//initialize callback
	gpio_init_callback(&button_0_data, button_0_handler, BIT(button_0.pin));
	//add callback
	gpio_add_callback(button_0.port, &button_0_data);
	debug_log("Set up button 0 ok\n");
	
	//Button 1
	if (!gpio_is_ready_dt(&button_1)) {
		debug_log("Error: button 0 is not ready\n");
		return -1;
	}
	//configure button as input
	ret = gpio_pin_configure_dt(&button_1, GPIO_INPUT);
	if (ret != 0) {
		debug_log("Error: failed to configure pin\n");
		return -1;
	}
	//configure button interrupt
	ret = gpio_pin_interrupt_configure_dt(&button_1, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		debug_log("Error: failed to configure interrupt on pin\n");
		return -1;
	}
	//initialize callback
	gpio_init_callback(&button_1_data, button_1_handler, BIT(button_1.pin));
	//add callback
	gpio_add_callback(button_1.port, &button_1_data);

	//button 2
	if (!gpio_is_ready_dt(&button_2)) {
		debug_log("Error: button 0 is not ready\n");
		return -1;
	}
	//configure button as input
	ret = gpio_pin_configure_dt(&button_2, GPIO_INPUT);
	if (ret != 0) {
		debug_log("Error: failed to configure pin\n");
		return -1;
	}
	//configure button interrupt
	ret = gpio_pin_interrupt_configure_dt(&button_2, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		debug_log("Error: failed to configure interrupt on pin\n");
		return -1;
	}
	//initialize callback
	gpio_init_callback(&button_2_data, button_2_handler, BIT(button_2.pin));
	//add callback
	gpio_add_callback(button_2.port, &button_2_data);

	//button 3
	if (!gpio_is_ready_dt(&button_3)) {
		debug_log("Error: button 0 is not ready\n");
		return -1;
	}
	//configure button as input
	ret = gpio_pin_configure_dt(&button_3, GPIO_INPUT);
	if (ret != 0) {
		debug_log("Error: failed to configure pin\n");
		return -1;
	}
	//configure button interrupt
	ret = gpio_pin_interrupt_configure_dt(&button_3, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		debug_log("Error: failed to configure interrupt on pin\n");
		return -1;
	}
	//initialize callback
	gpio_init_callback(&button_3_data, button_3_handler, BIT(button_3.pin));
	//add callback
	gpio_add_callback(button_3.port, &button_3_data);

	//button 4
	if (!gpio_is_ready_dt(&button_4)) {
		debug_log("Error: button 0 is not ready\n");
		return -1;
	}
	//configure button as input
	ret = gpio_pin_configure_dt(&button_4, GPIO_INPUT);
	if (ret != 0) {
		debug_log("Error: failed to configure pin\n");
		return -1;
	}
	//configure button interrupt
	ret = gpio_pin_interrupt_configure_dt(&button_4, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		debug_log("Error: failed to configure interrupt on pin\n");
		return -1;
	}
	//initialize callback
	gpio_init_callback(&button_4_data, button_4_handler, BIT(button_4.pin));
	//add callback
	gpio_add_callback(button_4.port, &button_4_data);
	
	return 0;
}

//button interrupt handler. Button 0 = pause and resume the traffic light sequence
void button_0_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{	
	debug_log("Button 0 pressed\n");
/* if we are not in pause: 1. Save current state
2. Change state to paused */
if (!sequence_paused){
	sequence_paused = true;
	debug_log("Pause ON \n");
}
/* if we are already in pause:
restore the previous state */
else{
	sequence_paused = false;
	debug_log("Pause OFF\n");
}
}

// Button 1 = red led on / off
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
		debug_log("Yellow blinking ON\n");

		gpio_pin_set_dt(&red, 0);
		gpio_pin_set_dt(&green, 0);

		if (!yellow_blink_thread_started) {

			// start the yellow blinking thread for the first time
			k_thread_start(&yellow_blink_thread_data);
			yellow_blink_thread_started = true;

		} else {

			//resume the tread if it was previously suspended
			k_thread_resume(&yellow_blink_thread_data);
		}
	}
	else {
		debug_log("Yellow blinking OFF\n");
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
			 //debug_log("Received: %c\n",rc);
			// If character is not newline, add to UART message buffer
			if (rc != '\r') {
				assert(uart_msg_cnt < 99);
				uart_msg[uart_msg_cnt] = rc;
				uart_msg_cnt++;
			// Character is newline, copy dispatcher data and put to FIFO buffer
			} else {
				debug_log("UART msg: %s\n", uart_msg);

                //Fifo puskurin käyttö
				struct data_t *buf = k_malloc(sizeof(struct data_t));
				if (buf == NULL) {
					debug_log("Memory allocation failed\n");
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
		
		assert(commands_out[count].color == 'R' ||
			   commands_out[count].color == 'Y' ||
			   commands_out[count].color == 'G');

		assert(commands_out[count].time > 0);	   
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
		debug_log("Dispatcher: %s\n", rec_item->msg);
		
		num_commands = parse_uart_string(rec_item->msg, commands, 10);
		
		assert(num_commands <= 10);
		
		//vapauta uart
		k_free(rec_item);
		sequence_total_us = 0;	

	for (int i = 0; i < num_commands; i++){
		assert(commands[i].color == 'R' ||
			   commands[i].color == 'Y' ||
			   commands[i].color == 'G');

		assert(commands[i].time > 0);	

		debug_log("commands: %c,%d\n", commands[i].color, commands[i].time);   

		while(sequence_paused){
			k_msleep(50);
		}

		if (commands[i].color == 'R'){
			debug_log("punanen alkaa\n");
			
			current_led_thread = &red_thread_data;
			k_thread_create(&red_thread_data, red_stack,K_THREAD_STACK_SIZEOF(red_stack),red_led_task,&commands[i].time,NULL,NULL,PRIORITY,0,K_NO_WAIT);
			debug_log("punanen lahetetty\n");
					
			k_thread_join(&red_thread_data, K_FOREVER);
			current_led_thread = NULL;
			
			debug_log("punanen vapautettu\n");

		}
		else if (commands[i].color == 'Y'){
			debug_log("keltanen alkaa\n");

			current_led_thread = &yellow_thread_data;			
			k_thread_create(&yellow_thread_data, yellow_stack, K_THREAD_STACK_SIZEOF(yellow_stack),yellow_led_task,&commands[i].time,NULL,NULL,PRIORITY,0,K_NO_WAIT);
			debug_log("keltanen lahetetty\n");
			
			k_thread_join(&yellow_thread_data, K_FOREVER);
			debug_log("keltanen vapautettu\n");
		
			current_led_thread = NULL;
		}
		else if (commands[i].color == 'G'){
			debug_log("vihrea alkaa\n");

			current_led_thread = &green_thread_data;			
			k_thread_create(&green_thread_data, green_stack,K_THREAD_STACK_SIZEOF(green_stack),green_led_task,&commands[i].time,NULL,NULL,PRIORITY,0,K_NO_WAIT);
			debug_log("vihrea lahetetty\n");
			
			k_thread_join(&green_thread_data, K_FOREVER);
			debug_log("vihreä vapautettu\n");
			
			current_led_thread = NULL;
		}
		else{
			
			debug_log("unknown color: \n");
	
			continue;
		}
		
	}	
	// print the total measured execution time of all the led tasks in this sequence 
	
	debug_log("SEQUENCE TOTAL: %llu us\n", sequence_total_us);
			
}
}

// Task to handle red led
void red_led_task(void *aika, void *, void*) {
	int *time;
	time =(int*)aika;
	
	timing_start();
	timing_t red_start_time = timing_counter_get();

	
	debug_log("Red led thread started\n");
	debug_log("RED: %d ms\n",*time);
			
	// 1. set led on 
	gpio_pin_set_dt(&red,1);
	debug_log("Red on\n");
		
	k_msleep(*time);

	// 3. set led off
	gpio_pin_set_dt(&red,0);
	debug_log("Red off\n");
		
	timing_t red_end_time = timing_counter_get();

	uint64_t red_cycles = timing_cycles_get(&red_start_time, &red_end_time);

	uint64_t red_ns = timing_cycles_to_ns(red_cycles);

	uint64_t red_us =red_ns / 1000;

	timing_stop();

	// add the red task execution time to the total sequence time
	sequence_total_us += red_us;
		
	debug_log("RED task time: %llu us\n", red_us);
		
}
	
	

// Task to handle yellow led
void yellow_led_task(void *aika, void *, void*) {
	int *time;
	time =(int*)aika;
	
	timing_start();
	timing_t yellow_start_time = timing_counter_get();
	
	debug_log("yellow led thread started\n");
	
	debug_log("yellow:  %d ms\n",*time);
		
		
	// 1. set led on 	
	set_yellow(true);
		
	debug_log("Yellow on\n");
	
	k_msleep(*time);

	// 3. set led off
	set_yellow(false);
	
	debug_log("Yellow off\n");

	timing_t yellow_end_time = timing_counter_get();

	uint64_t yellow_cycles = timing_cycles_get(&yellow_start_time, &yellow_end_time);

	uint64_t yellow_ns = timing_cycles_to_ns(yellow_cycles);

	uint64_t yellow_us = yellow_ns / 1000;

	timing_stop();

	sequence_total_us += yellow_us;
		
	debug_log("YELLOW task time: %llu us\n", yellow_us);
		

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

	timing_start();
	timing_t green_start_time = timing_counter_get();
	
	debug_log("green led thread started\n");
	
	debug_log("green:  %d ms\n",*time);
			
	// 1. set led on 
	gpio_pin_set_dt(&green,1);
	
	debug_log("green on\n");
	
	k_msleep(*time);

	// 3. set led off
	gpio_pin_set_dt(&green,0);
	debug_log("green off\n");
	
	timing_t green_end_time = timing_counter_get();

	uint64_t green_cycles = timing_cycles_get(&green_start_time, &green_end_time);

	uint64_t green_ns = timing_cycles_to_ns(green_cycles);

	uint64_t green_us = green_ns / 1000;

	timing_stop();

	sequence_total_us += green_us;
		
	debug_log("GREEN task time: %llu us\n", green_us);
		

	}