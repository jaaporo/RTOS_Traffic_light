#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
/* Tavoittelen täysiä pisteitä.
1p = taskien synkronointi tilakoneella:
- Ohjelmassa on main-task, punaisen, keltaisen ja vihreän LEDin taskit
- Globaali led_state toimii tilakoneena
- Sekvenssi on punaine -> keltainen -> vihreä -> punainen
- Keltainen toteutetaan sytyttämällä punainen ja vihreä LED yhtä aikaa

+1p = taskien suorituksen keskeytys
- Nappi 0 toimii pause nappina
- Ensimmäinne painallus tallentaa nykyisen tilan ja siirtyy pause tilaan (led-state = 4)
- Toinen painallus palauttaa aikaisemman tilan, joten sekvenssi jatkuu siitä väristä, jossa se 
keskeytettiin

+1p = ohjaus painonapeilla
- Nappi 1 -> punainen LED on/off
- Nappi 2 -> keltainen LED on/off
- Nappi 3 -> vihreä LED on/off
- Nappi 4 -> käynnistää keltaisen LED vilkkumisen ensimmäisellä painalluksella ja pysäyttää
sen toisella painalluksella*/
// Led pin configurations
static const struct gpio_dt_spec red = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
//static const struct gpio_dt_spec yellow = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

//button configuration
#define BUTTON_0 DT_ALIAS(sw0)
#define BUTTON_1 DT_ALIAS(sw1)
#define BUTTON_2 DT_ALIAS(sw2)
#define BUTTON_3 DT_ALIAS(sw3)
#define BUTTON_4 DT_ALIAS(sw4)
//nappi 0 -> pause /jatka normaalia sekvenssiä
//nappi 1 -> punainen LED on/off
//nappi 2 -> keltainen LED on/off
//nappi 3 -> vihreä LED on/off
//nappi 4 -> keltainen vilkkuminen on/off

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

//led thread initialization
#define STACKSIZE 500
#define PRIORITY 5
void red_led_task(void *, void *, void*);
K_THREAD_DEFINE(red_thread,STACKSIZE,red_led_task,NULL,NULL,NULL,PRIORITY,0,0);

void yellow_led_task(void *, void *, void*);
K_THREAD_DEFINE(yellow_thread,STACKSIZE,yellow_led_task,NULL,NULL,NULL,PRIORITY,0,0);

void green_led_task(void *, void *, void*);
K_THREAD_DEFINE(green_thread,STACKSIZE,green_led_task,NULL,NULL,NULL,PRIORITY,0,0);

// state machine
volatile int led_state = 0; 
// 1 = punanen
// 2 = keltainen
// 3 = vihreä
// 4 = pause
// 5 = keltainen vilkkuu
// Stores the LED state before pause
int previous_state = 0;

//manual LED control
volatile bool red_manual = false;
volatile bool yellow_manual = false;
volatile bool green_manual = false;

//yellow blinking
volatile bool yellow_blinking = false;

//function declarations
int init_led();
int init_button();

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

//Function for yellow LED. Yellow = red + green
void set_yellow(bool on){
	gpio_pin_set_dt(&red,on);
	gpio_pin_set_dt(&green,on);
}
// Main program
int main(void)
{	//initialize LEDs
	int ret = init_led();
	if (ret < 0){
		return 0;
	}
	//initialize button
	ret = init_button();
	if (ret < 0){
		return 0;
	}
	led_state = 1;

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
if (led_state != 4){
	previous_state = led_state;
	led_state = 4;
	printk("Pause ON \n");
}
/* if we are already in pause:
restore the previous state */
else{
	led_state = previous_state;
	printk("Pause OFF\n");
}
}

// Button 1 = red led 0n / off
void button_1_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	led_state = 4;
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
	led_state = 4;
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
	led_state = 4;
	green_manual = !green_manual;
	if (green_manual){
		gpio_pin_set_dt(&green,1);
	}
	else{
		gpio_pin_set_dt(&green,0);
	}
}

// Button 4 = yellow blinking
void button_4_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	yellow_blinking = !yellow_blinking;
	//first press -> start yellow blinking
	if (yellow_blinking){
	//stop normal sequence
		led_state = 5;
	//turn red and green off
	gpio_pin_set_dt(&red,0);
	gpio_pin_set_dt(&green,0);
	}
	//second press -> stop blinking
	else{
		//stop yellow blinking
		set_yellow(false);
		//start normal sequence again
		led_state = 1;
	}
}


// Task to handle red led
void red_led_task(void *, void *, void*) {
	
	printk("Red led thread started\n");
	while (true) {
		if (led_state == 1){
			// 1. set led on 
			gpio_pin_set_dt(&red,1);
			printk("Red on\n");
			// 2. sleep for 2 seconds
			k_sleep(K_SECONDS(1));
			// 3. set led off
			gpio_pin_set_dt(&red,0);
			printk("Red off\n");
			// 4. sleep for 2 seconds
			k_sleep(K_SECONDS(1));
			if (led_state == 1){
				led_state = 2;
			}
		}
		k_msleep(100);
	}
}

// Task to handle yellow led
void yellow_led_task(void *, void *, void*) {
	
	printk("yellow led thread started\n");
	while (true) {
		if (led_state == 2){
			// 1. set led on 
			set_yellow(true);
			printk("yellow on\n");
			// 2. sleep for 2 seconds
			k_sleep(K_SECONDS(1));
			// 3. set led off
			set_yellow(false);
			printk("yellow off\n");
			// 4. sleep for 2 seconds
			k_sleep(K_SECONDS(1));
			if (led_state == 2){
				led_state = 3;
			}
		}
		//button 4 = yellow blinking
		if (led_state == 5){
			//yellow on
			set_yellow(true);
			k_sleep(K_SECONDS(1));
			//yellow off
			set_yellow(false);
			k_sleep(K_SECONDS(1));
		}
		k_msleep(100);
	}
}
// Task to handle green led
void green_led_task(void *, void *, void*) {
	
	printk("Green led thread started\n");
	while (true) {
		if (led_state == 3){
			// 1. set led on 
			gpio_pin_set_dt(&green,1);
			printk("Green on\n");
			// 2. sleep for 2 seconds
			k_sleep(K_SECONDS(1));
			// 3. set led off
			gpio_pin_set_dt(&green,0);
			printk("Green off\n");
			// 4. sleep for 2 seconds
			k_sleep(K_SECONDS(1));
			if (led_state == 3){
				led_state = 1;
			}
		}
		k_msleep(100);
	}
}