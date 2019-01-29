#include <msp430g2553.h>
/*
 * File: REM_Sleep_Cycle
 * Author: James Drewelow
 * Date: 01/09/2018
 * Description: Program to detect REM sleep by reading EOG sensor data and
 * determining frequency of eye movement. Data is transmitted via UART to a BLE
 * module to be received and plotted on an Android device. The MCU will listen
 * for commands from the Android device to blink LEDs and power a vibration motor.
 */

//-----------------Initialize variables----------------------//
int LED_cue = 0;
int motor_cue = 1;
int sleep_mode = 0;
int REM_bool = 0;               //boolean variable to signal REM sleep detected
int cmd_from_android = 0;       //variable for receiving BT commands from Android device
int LED_brightness = 0;
int freq_counter = 0;
volatile int prev_freq_state;
volatile int freq_state;
int timer_count = 0;

int main(void)
{

     WDTCTL = WDTPW | WDTHOLD;	       // stop watchdog timer

    //----------------Configure Timer_A0----------------------//
    TACCTL0 = CCIE;
    TA0CTL |= TASSEL_2 + MC_1 + ID_3;
    TA0CCR0 = 1245;                    // 1245*100 interrupts = 1 second

    //----------------Configure Timer_A1----------------------//
    TA1CCTL1 = OUTMOD_7;
    TA1CCR0 = 100;
    TA1CCR1 = 0;
    TA1CTL = TASSEL_2 + MC_1;

    //------------------- Configure the Clocks -------------------//
    DCOCTL  = 0;                       // Select lowest DCOx and MODx settings
    BCSCTL1 = CALBC1_1MHZ;             // Set range
    DCOCTL  = CALDCO_1MHZ;             // Set DCO step + modulation

    //------------------- Configure pins--------------------------//
    P1OUT = 0;                         //clear P1OUT
    P2DIR = 0;                         //clear P2DIR
    P1SEL  |=  BIT1 + BIT2;            // P1.1 UCA0RXD input
    P1SEL2 |=  BIT1 + BIT2;            // P1.2 UCA0TXD output
    P1DIR  |=  BIT3 + BIT4   + BIT7 ;  //P1.3,1.4,1.7 set as outputs
    P1DIR  &=  ~BIT0 + ~BIT5;          //P1.0,1.5 set as input
    P2DIR &= ~BIT1 + ~BIT2;              //P2.1 and 2.2 set as inputs (turned off by default)
    P2SEL |= BIT1 + BIT2;              //P2.1 and 2.2 set for PWM
    P1OUT |= BIT4;//set reset high

    //--------------------Configure ADC--------------------------//
    ADC10CTL0 &= ~ENC;
    ADC10CTL1 = INCH_0 + SHS_0 + ADC10DIV_0 + ADC10SSEL_0 + CONSEQ_1;
    ADC10CTL0 = ADC10SHT_1 + ADC10ON + ADC10SR + MSC;
    ADC10AE0 |= BIT0;

    //------------ Configuring the UART(USCI_A0) -----------------//

    UCA0CTL1 |=  UCSSEL_2 + UCSWRST;  // USCI Clock = SMCLK,USCI_A0 disabled
    UCA0BR0   =  104;                 // 104 From datasheet table
    UCA0BR1   =  0;                   // -selects baudrate =9600,clk = SMCLK
    UCA0MCTL  =  UCBRS_1;             // Modulation value = 1 from datasheet
    UCA0CTL1 &= ~UCSWRST;             // Clear UCSWRST to enable USCI_A0

    //---------------- Enabling the interrupts ------------------//

    IE2 |= UCA0RXIE;                  // Enable Rx interrupt
    _BIS_SR(GIE);                     // Enable the global interrupt

    //------------------------- Main ----------------------------//
    while(1)
    {

       switch(cmd_from_android) {
             case 0x44 :
             {
                 __delay_cycles(150000);
                 LED_brightness = cmd_from_android;
                 cmd_from_android = 0x00;
                 TA1CCR1 = LED_brightness;
                 __delay_cycles(1000000);
                 TA1CCR1 = 0;
             break;
             }
             case 0x49 :
             {
                 sleep_mode = 1;
                 cmd_from_android = 0;
             }
             break;
          }

       while(sleep_mode) //if the transmit interrupt is enabled
       {

           if(cmd_from_android == 0x4A)
           {
               TACCTL0 &= ~CCIE; //disable timer A0 for testing
               REM_bool = 1;
               cmd_from_android = 0;
           }

            if(REM_bool)
            {
                IE2 |= UCA0TXIE;
            }

            TA1CCR1 = LED_brightness;
            while(REM_bool)
            {
                ADC10CTL0 |= ENC + ADC10SC;
                while(ADC10CTL1 & ADC10BUSY);
                ADC10CTL0 &= ~ENC;

                switch(cmd_from_android){
                case 0x4D:
                {
                    P2DIR |= BIT1; //lights on
                }
                case 0x4E:
                {
                    P2DIR &= ~BIT1; //lights off
                }
                case 0x4F:
                {
                    P2DIR |= BIT2;              //P2.1 and 2.2 set as output
                }
                case 0x50:
                {
                    P2DIR &= ~BIT2;              //P2.1 and 2.2 set as output
                }
                case 0x51:
                {
                    P1OUT |= BIT7;
                }
                case 0x52:
                {
                    P1OUT &= ~BIT7;
                }
                default:
                {}

                }

                if(cmd_from_android == 0x4B)
                {
                    REM_bool = 0;
                    sleep_mode = 0;
                    IE2 &= ~UCA0TXIE;
                }

            }
            P1OUT &= ~BIT7; //turn off motor
            P2DIR &= ~BIT1 + ~BIT2;              //P2.1 and 2.2 set as inputs (turned off)

            if(cmd_from_android == 0x4C) //arbitrary command from android to exit sleep mode
            {
                sleep_mode = 0;
                cmd_from_android = 0;
                IE2 &= ~UCA0TXIE;
            }

       }

    }

}

//------------------------Timer A0 interrupt-----------------------//

#pragma vector=TIMER0_A0_VECTOR
__interrupt void TIMER0A0_ISR(void) //Maybe move some of this code into main
{
    freq_state = P1IN & BIT5;
    if(timer_count == 100)
    {
       if(freq_counter >= 20 )  //10 Hz
       {
           REM_bool = 1;
       }

       else
       {
           REM_bool = 1; //set to one for testing main loop
       }

       freq_counter = 0;
       timer_count = 0;
    }

    else
    {
        if(freq_state != prev_freq_state)
               {
                freq_counter++;
               }

        timer_count++;
    }

    prev_freq_state = freq_state;
}


//-----------Transmit and Receive interrupts-----------------------//

#pragma vector = USCIAB0TX_VECTOR
__interrupt void TransmitInterrupt(void)
{

  if(REM_bool)                      //if the user is in REM sleep, send REM data
  {
  UCA0TXBUF = ADC10MEM >> 2;        // UART stored and sent in UCA0TXBUF, divide by 4 for easier transmission
  }

}

#pragma vector = USCIAB0RX_VECTOR
__interrupt void ReceiveInterrupt(void)
{
    cmd_from_android = UCA0RXBUF;
}
