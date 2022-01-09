/**
 * @file   practice1.c
 * @author Derek Molloy
 * @date   15 December 2021
 * @brief  A kernel module for controlling a GPIO LED/button pair. The device mounts devices via
 * sysfs /sys/class/gpio/gpio115 and gpio49. Therefore, this test LKM circuit assumes that an LED
 * is attached to GPIO 49 which is on P9_23 and the button is attached to GPIO 115 on P9_27. There
 * is no requirement for a custom overlay, as the pins are in their default mux mode states.
 * @see http://www.derekmolloy.ie/
*/
 
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>                 // Required for the GPIO functions
#include <linux/interrupt.h>            // Required for the IRQ code
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Derek Molloy");
MODULE_DESCRIPTION("A Button/LED test driver for the BBB");
MODULE_VERSION("0.1");
 
static unsigned int gpioLED1 = 14;       ///< hard coding the LED gpio to pin 8 (GPIO14)
static unsigned int gpioLED2 = 15;	///< hard coding the second LED gpio to pin 10 (GPIO15)
static unsigned int gpioButtonA = 8;	///< hard coding the button A gpio to pin 16 (GPIO23)
static unsigned int gpioButtonB = 7;	///< hard coding the button B gpio to pin 18 (GPIO24)
static unsigned int gpioButtonC = 23;	///< hard coding the button C gpio to pin 24 (GPIO8)
static unsigned int gpioButtonD = 24;   ///< hard coding the button D gpio to pin 26 (GPIO7)
static unsigned int irqNumberA;          ///< Used to share the IRQ number within this file
static unsigned int irqNumberB;
static unsigned int irqNumberC;
static unsigned int irqNumberD;
static unsigned int numberPressesA = 0;  ///< For information, store the number of button A presses
static unsigned int numberPressesB = 0;	///< For information, store the number of button B presses
static unsigned int numberPressesC = 0;	///< For information, store the number of button C presses
static unsigned int numberPressesD = 0;	///< For information, store the number of button D presses
static bool     led1On = 0;          ///< Is the LED 1 on or off? Used to invert its state (off by default)
static bool	led2On = 0;	     ///< Is the LED 2 on or off?
static char *argv1[] = {"/usr/bin/buttonScripts/buttonA.sh", NULL};
static char *argv2[] = {"/usr/bin/buttonScripts/buttonB.sh", NULL};
static char *argv3[] = {"/usr/bin/buttonScripts/buttonC.sh", NULL};
static char *argv4[] = {"/usr/bin/buttonScripts/buttonD.sh", NULL};
static char *envp[] = {"HOME=/", NULL};

/// Function prototype for the custom IRQ handler function -- see below for the implementation
static irq_handler_t  ebbgpio_irq_handlerA(unsigned int irq, void *dev_id, struct pt_regs *regs);
static irq_handler_t  ebbgpio_irq_handlerB(unsigned int irq, void *dev_id, struct pt_regs *regs);
static irq_handler_t  ebbgpio_irq_handlerC(unsigned int irq, void *dev_id, struct pt_regs *regs);
static irq_handler_t  ebbgpio_irq_handlerD(unsigned int irq, void *dev_id, struct pt_regs *regs);
 
/** @brief The LKM initialization function
 *  The static keyword restricts the visibility of the function to within this C file. The __init
 *  macro means that for a built-in driver (not a LKM) the function is only used at initialization
 *  time and that it can be discarded and its memory freed up after that point. In this example this
 *  function sets up the GPIOs and the IRQ
 *  @return returns 0 if successful
 */
static int __init ebbgpio_init(void){
   int resultA = 0;
   int resultB = 0;
   int resultC = 0;
   int resultD = 0;
   printk(KERN_INFO "GPIO_TEST: Initializing the GPIO_TEST LKM\n");
   // Is the GPIO a valid GPIO number (e.g., the BBB has 4x32 but not all available)
   if (!gpio_is_valid(gpioLED1)){
      printk(KERN_INFO "GPIO_TEST: invalid LED 1 GPIO\n");
      return -ENODEV;
   }

   if (!gpio_is_valid(gpioLED2)){
	   printk(KERN_INFO "GPIO_TEST: invalid LED 2 GPIO\n");
	   return -ENODEV;
   }
   // Going to set up the LED. It is a GPIO in output mode and will be on by default
   led1On = true;
   led2On = true;
   gpio_request(gpioLED1, "sysfs");          // gpioLED is hardcoded to 49, request it
   gpio_request(gpioLED2, "sysfs");
   gpio_direction_output(gpioLED1, led1On);   // Set the gpio to be in output mode and on
   gpio_direction_output(gpioLED2, led2On);
// gpio_set_value(gpioLED, ledOn);          // Not required as set by line above (here for reference)
   gpio_export(gpioLED1, false);             // Causes gpio49 to appear in /sys/class/gpio
   gpio_export(gpioLED2, false);
                     // the bool argument prevents the direction from being changed
   gpio_request(gpioButtonA, "sysfs");       // Set up the gpioButton
   gpio_direction_input(gpioButtonA);        // Set the button GPIO to be an input
   gpio_set_debounce(gpioButtonA, 200);      // Debounce the button with a delay of 200ms
   gpio_request(gpioButtonB, "sysfs");
   gpio_direction_input(gpioButtonB);
   gpio_set_debounce(gpioButtonB, 200);
   gpio_request(gpioButtonC, "sysfs");
   gpio_direction_input(gpioButtonC);
   gpio_set_debounce(gpioButtonC, 200);
   gpio_request(gpioButtonD, "sysfs");
   gpio_direction_input(gpioButtonD);
   gpio_set_debounce(gpioButtonD, 200);

   // Perform a quick test to see that the button is working as expected on LKM load
   printk(KERN_INFO "GPIO_TEST: The button A state is currently: %d\n", gpio_get_value(gpioButtonA));
   printk(KERN_INFO "GPIO_TEST: The button B state is currently: %d\n", gpio_get_value(gpioButtonB));
   printk(KERN_INFO "GPIO_TEST: The button C state is currently: %d\n", gpio_get_value(gpioButtonC));
   printk(KERN_INFO "GPIO_TEST: The button D state is currently: %d\n", gpio_get_value(gpioButtonD));
 
   // GPIO numbers and IRQ numbers are not the same! This function performs the mapping for us
   irqNumberA = gpio_to_irq(gpioButtonA);
   printk(KERN_INFO "GPIO_TEST: The button A is mapped to IRQ: %d\n", irqNumberA);
   irqNumberB = gpio_to_irq(gpioButtonB);
   printk(KERN_INFO "GPIO_TEST: The button B is mapped to IRQ: %d\n", irqNumberB);
   irqNumberC = gpio_to_irq(gpioButtonC);
   printk(KERN_INFO "GPIO_TEST: The button C is mapped to IRQ: %d\n", irqNumberC);
   irqNumberD = gpio_to_irq(gpioButtonD);
   printk(KERN_INFO "GPIO_TEST: The button D is mapped to IRQ: %d\n", irqNumberD);

 
   // This next call requests an interrupt line
   resultA = request_irq(irqNumberA,             // The interrupt number requested
                        (irq_handler_t) ebbgpio_irq_handlerA, // The pointer to the handler function below
                        IRQF_TRIGGER_RISING,   // Interrupt on rising edge (button press, not release)
                        "ebb_gpio_handler",    // Used in /proc/interrupts to identify the owner
                        NULL);                 // The *dev_id for shared interrupt lines, NULL is okay
 
   resultB = request_irq(irqNumberB,             // The interrupt number requested
                        (irq_handler_t) ebbgpio_irq_handlerB, // The pointer to the handler function below
                        IRQF_TRIGGER_RISING,   // Interrupt on rising edge (button press, not release)
                        "ebb_gpio_handler",    // Used in /proc/interrupts to identify the owner
                        NULL);                 // The *dev_id for shared interrupt lines, NULL is okay
   resultC = request_irq(irqNumberC,             // The interrupt number requested
                        (irq_handler_t) ebbgpio_irq_handlerC, // The pointer to the handler function below
                        IRQF_TRIGGER_RISING,   // Interrupt on rising edge (button press, not release)
                        "ebb_gpio_handler",    // Used in /proc/interrupts to identify the owner
                        NULL);                 // The *dev_id for shared interrupt lines, NULL is okay
   resultD = request_irq(irqNumberD,             // The interrupt number requested
                        (irq_handler_t) ebbgpio_irq_handlerD, // The pointer to the handler function below
                        IRQF_TRIGGER_RISING,   // Interrupt on rising edge (button press, not release)
                        "ebb_gpio_handler",    // Used in /proc/interrupts to identify the owner
                        NULL);                 // The *dev_id for shared interrupt lines, NULL is okay
  
   printk(KERN_INFO "GPIO_TEST: The interrupt request result is: %d\n", resultA);
   return resultA;
}
 
/** @brief The LKM cleanup function
 *  Similar to the initialization function, it is static. The __exit macro notifies that if this
 *  code is used for a built-in driver (not a LKM) that this function is not required. Used to release the
 *  GPIOs and display cleanup messages.
 */
static void __exit ebbgpio_exit(void){
   printk(KERN_INFO "GPIO_TEST: The button state is currently: %d\n", gpio_get_value(gpioButtonD));
   printk(KERN_INFO "GPIO_TEST: The button was pressed %d times\n", numberPressesD);
   gpio_set_value(gpioLED1, 0);              // Turn the LED off, makes it clear the device was unloaded
   gpio_set_value(gpioLED2, 0);
   gpio_unexport(gpioLED1);                  // Unexport the LED GPIO
   gpio_unexport(gpioLED2);
   free_irq(irqNumberA, NULL);               // Free the IRQ number, no *dev_id required in this case
   gpio_unexport(gpioButtonA);               // Unexport the Button GPIO
   free_irq(irqNumberB, NULL);
   gpio_unexport(gpioButtonB);
   free_irq(irqNumberC, NULL);
   gpio_unexport(gpioButtonC);
   free_irq(irqNumberD, NULL);
   gpio_unexport(gpioButtonD);
   gpio_free(gpioLED1);                      // Free the LED GPIO
   gpio_free(gpioLED2);
   gpio_free(gpioButtonA);
   gpio_free(gpioButtonB);
   gpio_free(gpioButtonC);
   gpio_free(gpioButtonD);                   // Free the Button GPIO
   printk(KERN_INFO "Button A has been pressed %d times.", numberPressesA);
   printk(KERN_INFO "Button B has been pressed %d times.", numberPressesB);
   printk(KERN_INFO "Button C has been pressed %d times.", numberPressesC);
   printk(KERN_INFO "Button D has been pressed %d times.", numberPressesD);
   printk(KERN_INFO "GPIO_TEST: Goodbye from the LKM!\n");
}
 
/** @brief The GPIO IRQ Handler function
 *  This function is a custom interrupt handler that is attached to the GPIO above. The same interrupt
 *  handler cannot be invoked concurrently as the interrupt line is masked out until the function is complete.
 *  This function is static as it should not be invoked directly from outside of this file.
 *  @param irq    the IRQ number that is associated with the GPIO -- useful for logging.
 *  @param dev_id the *dev_id that is provided -- can be used to identify which device caused the interrupt
 *  Not used in this example as NULL is passed.
 *  @param regs   h/w specific register values -- only really ever used for debugging.
 *  return returns IRQ_HANDLED if successful -- should return IRQ_NONE otherwise.
 */
static irq_handler_t ebbgpio_irq_handlerA(unsigned int irq, void *dev_id, struct pt_regs *regs){
   led1On = true;                          // Invert the LED state on each button press
   gpio_set_value(gpioLED1, led1On);          // Set the physical LED accordingly
   printk(KERN_INFO "GPIO_TEST: Interrupt! (button A state is %d)\n", gpio_get_value(gpioButtonA));
   call_usermodehelper(argv1[0], argv1, envp, UMH_NO_WAIT);
   numberPressesA++;                         // Global counter, will be outputted when the module is unloaded
   return (irq_handler_t) IRQ_HANDLED;      // Announce that the IRQ has been handled correctly
}
 
static irq_handler_t ebbgpio_irq_handlerB(unsigned int irq, void *dev_id, struct pt_regs *regs){
   led1On = false;                          // Invert the LED state on each button press
   gpio_set_value(gpioLED1, led1On);          // Set the physical LED accordingly
   printk(KERN_INFO "GPIO_TEST: Interrupt! (button B state is %d)\n", gpio_get_value(gpioButtonB));
   call_usermodehelper(argv2[0], argv2, envp, UMH_NO_WAIT);
   numberPressesB++;                         // Global counter, will be outputted when the module is unloaded
   return (irq_handler_t) IRQ_HANDLED;      // Announce that the IRQ has been handled correctly
}

static irq_handler_t ebbgpio_irq_handlerC(unsigned int irq, void *dev_id, struct pt_regs *regs){
   led2On = true;                          // Invert the LED state on each button press
   gpio_set_value(gpioLED2, led2On);          // Set the physical LED accordingly
   printk(KERN_INFO "GPIO_TEST: Interrupt! (button C state is %d)\n", gpio_get_value(gpioButtonC));
   call_usermodehelper(argv3[0], argv3, envp, UMH_NO_WAIT);
   numberPressesC++;                         // Global counter, will be outputted when the module is unloaded
   return (irq_handler_t) IRQ_HANDLED;      // Announce that the IRQ has been handled correctly
}

static irq_handler_t ebbgpio_irq_handlerD(unsigned int irq, void *dev_id, struct pt_regs *regs){
   led2On = false;                          // Invert the LED state on each button press
   gpio_set_value(gpioLED2, led2On);          // Set the physical LED accordingly
   printk(KERN_INFO "GPIO_TEST: Interrupt! (button D state is %d)\n", gpio_get_value(gpioButtonD));
   call_usermodehelper(argv4[0], argv4, envp, UMH_NO_WAIT);
   numberPressesD++;                         // Global counter, will be outputted when the module is unloaded
   return (irq_handler_t) IRQ_HANDLED;      // Announce that the IRQ has been handled correctly
}
/// This next calls are  mandatory -- they identify the initialization function
/// and the cleanup function (as above).
module_init(ebbgpio_init);
module_exit(ebbgpio_exit);
