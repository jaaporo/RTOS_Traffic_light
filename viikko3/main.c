#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
/* Kolmen pisteen tavoite
1p: Sekvenssin vastaanotto sarjaportista
+1p: Lisätty sekvenssin ajastus, sarjaportista voidaan syöttää sekvenssi ja 
aika jonka LEDit palaa esim R,500:Y,1000:G,500
+1p: refaktoroi ohjelma, niin että valotaskeissa ei ole superlooppeja
*/
/****************************
 * Remember to add line:
 * CONFIG_HEAP_MEM_POOL_SIZE=1024
 * to prj.conf
 ****************************/
// Thread initializations
#define STACKSIZE 500
#define PRIORITY 5

void dispatcher_task(void *, void *, void*);
void uart_task(void *, void *, void*);

void red_led_task(void *, void *, void*);
void yellow_led_task(void *, void *, void*);
void green_led_task(void *, void *, void*);

K_THREAD_STACK_DEFINE(red_stack, STACKSIZE);
struct k_thread red_thread_data;

K_THREAD_STACK_DEFINE(yellow_stack, STACKSIZE);
struct k_thread yellow_thread_data;

K_THREAD_STACK_DEFINE(green_stack, STACKSIZE);
struct k_thread green_thread_data;

K_THREAD_DEFINE(dis_thread,STACKSIZE,dispatcher_task,NULL,NULL,NULL,PRIORITY,0,0);
K_THREAD_DEFINE(uart_thread,STACKSIZE,uart_task,NULL,NULL,NULL,PRIORITY,0,0);

// Led pin configurations
static const struct gpio_dt_spec red = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

// UART initialization
#define UART_DEVICE_NODE DT_CHOSEN(zephyr_shell_uart)
static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);


//dispatcher_fifo on paikka, josta se saa uart komennon
K_FIFO_DEFINE(dispatcher_fifo);


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

	// Led pin initialization
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

/********************
 * Main task
 */
int main(void)
{
	int ret = init_uart();
	if (ret != 0) {
		printk("UART initialization failed!\n");
		return ret;
	}

	init_led();

	return 0;
}

/********************
 * UART task
 */
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
					return;
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
		printk("commands0: %c,%d , commands1: %c,%d\n", commands[0].color, commands[0].time, commands[1].color, commands[1].time);


		//vapauta uart
		k_free(rec_item);

	for (int i = 0; i < num_commands; i++){
		printk("commands: %c,%d\n", commands[i].color, commands[i].time);


		if (commands[i].color == 'R'){
			printk("punanen alkaa\n");

			k_thread_create(&red_thread_data, red_stack,K_THREAD_STACK_SIZEOF(red_stack),red_led_task,&commands[i].time,NULL,NULL,PRIORITY,0,K_NO_WAIT);
			
			printk("punanen\n");

			printk("punanen lahetetty\n");
					
			int ret = k_thread_join(&red_thread_data, K_FOREVER);
			printk("red join returned: %d\n", ret);
			printk("punanen vapautettu\n");

		}
		else if (commands[i].color == 'Y'){
			printk("keltanen alkaa\n");

						
			k_thread_create(&yellow_thread_data, yellow_stack, K_THREAD_STACK_SIZEOF(yellow_stack),yellow_led_task,&commands[i].time,NULL,NULL,PRIORITY,0,K_NO_WAIT);
			printk("keltanen\n");

			printk("keltanen lahetetty\n");

			k_thread_join(&yellow_thread_data, K_FOREVER);
			printk("keltanen vapautettu\n");
		}
		else if (commands[i].color == 'G'){
			printk("vihrea alkaa\n");
						
			k_thread_create(&green_thread_data, green_stack,K_THREAD_STACK_SIZEOF(green_stack),green_led_task,&commands[i].time,NULL,NULL,PRIORITY,0,K_NO_WAIT);
			printk("vihrea\n");

			printk("vihrea lahetetty\n");
			k_thread_join(&green_thread_data, K_FOREVER);
			printk("vihreä vapautettu\n");
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
