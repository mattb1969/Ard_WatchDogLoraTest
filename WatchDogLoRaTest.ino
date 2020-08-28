/*
 * Setup
 *  0. blink_led(250,250,5)
 *  1. Setup RTC for 15s
 *  1a. Set counter to 10
 *  1b. Setup Watchdog for 30st
 * 
 * loop
 *  blink_led(500,500)
 *  2. Go to sleep
 *  blink_led(500,500)
 * 
 * 3. trigger on RTC
 * 4. Reset RTC for 15s
 * 4a. counter --
 * 4b. if counter > 0 Reset watchdog
 * 
 * RTC Interrupt blink_led(100,100,3)
 * WTD Interrupt blink_led(100,100,2)
 * 
 * 
 * Added LoRaModem          modem - still worked
 * 
 * 
 */


#include <MKRWAN.h>
#include "WDTZero.h"
#include "RTCZero.h"

LoRaModem modem;
WDTZero wdt;

volatile bool rtc_triggered = false;

static const uint8_t rtc_timer = 15;
static const uint16_t wdt_timer = WDT_SOFTCYCLE32S;

volatile uint8_t rtccounter = 5;

String appEui = "70B3D57ED002EAB7";
String appKey = "26ECFE5535B812860CB51D3F29F4898E";

String DevAddr;
String NwkSKey;
String AppSKey;

void blink_led(int ontime = 100, int offtime = 50, int flashes = 1) {
    while (flashes > 0) {
        digitalWrite(LED_BUILTIN, HIGH);
        delayMicroseconds(ontime * 1000);
        digitalWrite(LED_BUILTIN, LOW);
        if (flashes > 1) delayMicroseconds(offtime * 1000); // If it is not the last flash, wait until returning to the top of the loop
        flashes--;
    }
    return;
}

void InitTC(void) {
    //Set Clock divider for GCLK4
    GCLK->GENDIV.reg = GCLK_GENDIV_DIV(4) | //Divide 32.768kHz / 2^(4 + 1)
            GCLK_GENDIV_ID(4); //GCLK4
    while (GCLK->STATUS.bit.SYNCBUSY); //Wait for the settings to be synchronized

    //Configure Clock Generator 4 to give output to Timer modules to generate PWM signal
    GCLK->GENCTRL.reg = GCLK_GENCTRL_RUNSTDBY | // Set GCLK4 to run in standby mode
            GCLK_GENCTRL_OE | // Output GCLK4 on D6 (PA20)
            GCLK_GENCTRL_DIVSEL | // Set divisor to 2^(4 + 1)
            GCLK_GENCTRL_IDC | //50-50 Duty
            GCLK_GENCTRL_GENEN | //Enable generic clock generator
            //GCLK_GENCTRL_SRC_DFLL48M |  //48MHZ clock as the input of the general clock generator as source
            GCLK_GENCTRL_SRC_OSCULP32K | // Use the Ultra Low Power 32kHz oscillator
            GCLK_GENCTRL_ID(4); //Select GCLK 4*/

    //Connect GCLK4 output to TC modules
    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_CLKEN | //Enable generic clock
            GCLK_CLKCTRL_GEN_GCLK4 | //Select GCLK4
            GCLK_CLKCTRL_ID_TC4_TC5; //Feed GCLK4 output to TC4 & TC5

    TC4->COUNT16.CTRLA.reg |= TC_CTRLA_PRESCSYNC_PRESC | // Trigger next cycle on prescaler clock (not GCLK)
            TC_CTRLA_RUNSTDBY | // Set the timer to run in standby mode
            TC_CTRLA_PRESCALER_DIV1024 | // Division factor 1024
            TC_CTRLA_WAVEGEN_MFRQ | // Enable Matched Frequency Waveform generation
            TC_CTRLA_MODE_COUNT16; // Enable 16-bit COUNT mode

    TC4->COUNT16.CC[0].reg = rtc_timer; //TOP value for 15s
    while (TC4->COUNT16.STATUS.bit.SYNCBUSY); //Wait for synchronization

    // Configure TC4 interrupt request
    NVIC_SetPriority(TC4_IRQn, 0);
    NVIC_EnableIRQ(TC4_IRQn);

    TC4->COUNT16.INTENSET.reg = TC_INTENSET_OVF; // Enable TC4 interrupts

    TC4->COUNT16.CTRLA.bit.ENABLE = 1; // Enable TC4
    while (TC4->COUNT16.STATUS.bit.SYNCBUSY); // Wait for synchronization
}

void TC4_Handler() // Interrupt Service Routine (ISR) for timer TC4
{
    // Check for overflow (OVF) interrupt
    if (TC4->COUNT16.INTENSET.bit.OVF) {
        TC4->COUNT16.INTFLAG.bit.OVF = 1; // Clear the OVF interrupt flag

        // Put your timer overflow (OVF) code here:     
        rtc_triggered = true;
        if (rtccounter > 0) {
            // Counter is still greater than zero, so reset watchdog
            if (Serial) Serial.println("resetting WDT");
            blink_led(100, 100, rtccounter);
            rtccounter--;
            wdt.clear();
        } else {
            if (Serial) Serial.println("not reset WDT");
            rtccounter = 0;
            blink_led(100, 100);
        }
    }
}

void wdt_setup() {
    if (Serial) Serial.println("WDT Setup\n");
    wdt.setup(wdt_timer);
    wdt.attachShutdown(wdtinterrupt);
}

void wdt_off() {
    wdt.setup(WDT_OFF);
}

void wdt_clear() {
    wdt.clear();
}

void wdtinterrupt() {
    if (Serial) Serial.println("WDT Triggered, shutting down");
    blink_led(100, 100, 3);
    delayMicroseconds(500 * 1000);
}

bool lorawan_setup() {
    bool status = false;

    if (Serial) {
        Serial.println("Credentials being used");
        Serial.print("APP EUI:");
        Serial.println(appEui);
        Serial.print("APP Key:");
        Serial.println(appKey);
    }
    // Lora Comms modem start
    // change this to your regional band (eg. US915, AS923, ...)
    if (modem.begin(EU868)) {

        if (Serial) {
            Serial.print("Your module version is: ");
            Serial.println(modem.version());
            Serial.print("Your device EUI is: ");
            Serial.println(modem.deviceEUI());
        }
        int connected = modem.joinOTAA(appEui, appKey);
        if (!connected) {
            Serial.println("Something went wrong, unable to join LoRaWAN.");
            blink_led(500, 500, 7);
            status = false;
        } else {
            // Set poll interval to 60 secs.
            modem.minPollInterval(60);
            // NOTE: independently by this setting the modem will
            // not allow to send more than one message every 2 minutes,
            // this is enforced by firmware and can not be changed.

            DevAddr = modem.getDevAddr();
            NwkSKey = modem.getNwkSKey();
            AppSKey = modem.getAppSKey();
            status = true;
        }
    } else {
        Serial.println("Failed to start module");
        blink_led(500, 500, 7);
        status = false;
    }

    return status;
}

bool lorawan_senddata() {

    int payload_size = 5;
    char payload[payload_size + 1] = {0};


    strcpy(payload, "12345");
    // Send data over LoRa
    bool status = false;
    int err;
    if (Serial) {
        Serial.print(">");
        for (int i = 0; i < payload_size; i++) {
            Serial.print(payload[i], HEX);
            Serial.print("-");
        }
        Serial.println("<");
    }
    //blink_led(100,100,4);
    //  /* This appears to work as there is another entry on TTN*/
    //  modem.joinABP(DevAddr, NwkSKey, AppSKey);
    //  delayMicroseconds(250*1000);

    /* But it never gets to this line.*/
    //blink_led(100,100,5);

    modem.beginPacket();

    if (Serial) Serial.println("LoRa - Begin Packet transmission");

    modem.write(payload, payload_size);

    if (Serial) Serial.println("LoRa - Model Write Completed");

    err = modem.endPacket(false);

    if (Serial) Serial.println("LoRa - End Packet Completed with status:" + err);

    if (err > 0) {
        status = true;
        if (Serial) Serial.println("Message sent correctly!");
    } else {
        status = false;
        if (Serial) Serial.println("Error sending message");
    }
    return status;
}

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);

    int t = 20; //Initialize serial and wait for port to open, max 10 seconds
    Serial.begin(9600);
    while (!Serial) {
        delay(500);
        if ((t--) == 0) break;
    }
    blink_led(250, 250, 5);

    Serial.println("\nWDT / RTC Demo : Setup Soft Watchdog at 32S interval, RTC trigger at 15s");

    wdt_setup();

    InitTC();

    lorawan_setup();

    SYSCTRL->VREG.bit.RUNSTDBY = 1; // Keep the voltage regulator in normal configuration during run standby
    NVMCTRL->CTRLB.reg |= NVMCTRL_CTRLB_SLEEPPRM_DISABLED; // Disable auto power reduction during sleep - SAMD21 Errata 1.14.2

}

void loop() {

    //blink_led(500,500);
    //delay(1000);
    if (Serial) Serial.println("Starting main loop and going to sleep");

    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    __DSB();
    __WFI();

    //blink_led(500,500);
    //delay(1000);
    if (rtc_triggered) {
        lorawan_senddata();
        rtc_triggered = false;
    }

}
