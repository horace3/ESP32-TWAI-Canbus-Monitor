// C#  ESP32 Canbus monitor using a background worker
//

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.IO.Ports;
using System.Linq;
using System.Runtime.Remoting.Messaging;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace PIC24mechatronicsCsharp
{
    public partial class Form1 : Form
    {
        // delegate to transfer received data to TextBox
        public delegate void AddDataDelegate(String myString);
        public AddDataDelegate myDelegate, myDelegate2;
        private bool running;
        private bool transmitCANBUSmessage = false, setCANBUSparameters = false;
        private UInt32 Rx0mask = 0;
        private UInt32[] Rx0filter = { 0, 0 }, Rx1filter = { 0, 0, 0, 0 };
        private UInt32 canbusTranmitID = 0;
        private UInt64 canbusMessage;
        private char CANBUSspeed = '2';
        private string serialData;
        private bool canbusMonitorFound = false;

        public Form1()
        {
            this.SetStyle(ControlStyles.OptimizedDoubleBuffer | ControlStyles.AllPaintingInWmPaint, true);
            InitializeComponent();
            this.myDelegate = new AddDataDelegate(AddDataMethod);       // delegates to send serial data to TextBoxes
            this.myDelegate2 = new AddDataDelegate(AddDataMethod2);
            backgroundWorker1.RunWorkerAsync();

        }

        private void Form1_Load(object sender, EventArgs e)
        {
        }

        // thread handling communication with the ESP32 board
        private void backgroundWorker1_DoWork(object sender, DoWorkEventArgs e)
        {
            Console.WriteLine("background worker ");
            Thread.Sleep(1000);
            running = true;
            setCANBUSparameters = true;
            // Get a list of serial port names.
            string[] ports = SerialPort.GetPortNames();
            // terminalTextBox.AppendText("The following serial ports were found:" + Environment.NewLine);
            terminalTextBox.Invoke(this.myDelegate, new Object[] { "The following serial ports were found:" });
            Console.WriteLine("The following serial ports were found:");
            // Display each port name to the console. and send > to prompt for response
            foreach (string port in ports)
            {
                try                             // to open serial port
                {
                    terminalTextBox.Invoke(this.myDelegate, "attempting to open " + port + Environment.NewLine);
                    //terminalTextBox.Invoke(this.myDelegate, new Object[] { "  " + port });
                    serialPort1.PortName = port;
                    serialPort1.Open();
                }
                catch (Exception e1) { Console.WriteLine(e1); }

                if (serialPort1.IsOpen)             // if serial port is open OK
                {
                    serialPort1.Write(">");         // send prompt to see if canbus monitor is on this port
                    Thread.Sleep(3000);
                    if (canbusMonitorFound) break;  // found - continue communication
                    serialPort1.Close();
                }
                terminalTextBox.Invoke(this.myDelegate, "communication failed " + Environment.NewLine);
                Thread.Sleep(1000);
            }
            if (serialPort1.IsOpen)
            {
                terminalTextBox.Invoke(this.myDelegate, "serial port open OK" + Environment.NewLine);
                serialPort1.Write("I" + Environment.NewLine);     // send command to initialise ESP32 canbus monitor
            }
            else
            {
                terminalTextBox.Invoke(this.myDelegate, "Communication to Arduino failed! is it connected?" + Environment.NewLine);
                canbusReceivedMessageTextBox.Invoke(this.myDelegate, ">Communication to Arduino failed! " + Environment.NewLine
                                                              + "   is it connected?" + Environment.NewLine);
            }

            while (running)
            {
                {
                    //Text = "ESP32 Canbus monitor";      // display connected message
                    if (setCANBUSparameters) CANBUStransmitSetup();             // transmit CANBUS setup data to board?                                                                            //Console.WriteLine("status {0}", status);
                    if (transmitCANBUSmessage) CANBUStransmitMessageNow();      // transmit camnbus frame to monitor
                }
                //    Invalidate();                               // force redraw of graphics to display any drawing etc
                //  Update();
                //   Thread.Sleep(100);
            }
            // running has been set to false, send RESET to board and then close the Form
            Console.WriteLine("background worker exit ");
            //Close();
        }

        // Form is closing, if background working is running stop it and call close again
        private void Form1_FormClosing(object sender, FormClosingEventArgs e)
        {
            if (running) { running = false; e.Cancel = true; }
            serialPort1.Close();
        }

        private void serialPort1_ErrorReceived(object sender, System.IO.Ports.SerialErrorReceivedEventArgs e)
        {   }

        // text received from serial port
        private void serialPort1_DataReceived(object sender, System.IO.Ports.SerialDataReceivedEventArgs e)
        {
            try
            {
                //textBox1.AppendText(serialPort1.ReadExisting());  // not thread safe
                serialData = serialPort1.ReadLine();
                terminalTextBox.Invoke(this.myDelegate, new Object[] { serialData });
                // check for specific messages from ESP32 monitor
                if (serialData.Contains("ESP32 Canbus Monitor V1.0")) canbusMonitorFound = true;
                if (serialData.StartsWith("Rx")) canbusReceivedMessageTextBox.Invoke(this.myDelegate2, serialData);
                if (serialData.StartsWith("Baudrate")) canbusReceivedMessageTextBox.Invoke(this.myDelegate2, serialData);
                if (serialData.StartsWith("Mask")) canbusReceivedMessageTextBox.Invoke(this.myDelegate2, serialData);
            }
            catch (Exception e1) { Console.WriteLine(e1); }
        }

        // display serial data on textbox using delegate
        public void AddDataMethod(String myString)
        {
            if (myString.Length == 0) return;
            terminalTextBox.AppendText(myString + Environment.NewLine);
        }
        public void AddDataMethod2(String myString)
        {
            if (myString.Length == 0) return;
            canbusReceivedMessageTextBox.AppendText(myString + Environment.NewLine);
        }


        // Form has been invalidated - repaint it
        private void Form1_Paint(object sender, PaintEventArgs e)
        {
            Graphics g = e.Graphics;                                        // get graphics context
            g.Clear(Color.White);									        // clear the background to White
        }


        private void transmitButton_Click(object sender, EventArgs e)
        {
            transmitCANBUSmessage = true;       // transmit button has been clicked
        }

        private void canbisMessageTextBox_TextChanged(object sender, EventArgs e)
        {
            read64bitHexNumber(canbusMessageTextBox, ref canbusMessage);
        }

        // read a 29bit hex number (canbus ID) from TextBox, if not hex reject it, write number back to text box
        private UInt32 read29bitHexNumber(TextBox textbox, ref UInt32 number)
        {
            if (textbox.Text == "")                      // if TextBox is empty value is 0
                number = 0;
            else                                         // else convert string to hex
                try
                {
                    UInt32 ID = Convert.ToUInt32(textbox.Text, 16);
                    //Console.WriteLine(ID)
                    if (ID <= 0x1FFFFFFF) number = ID;  // if > 29bit reject it
                }
                catch (Exception) { }
            //Console.WriteLine("number {0:X}", number);
            textbox.Text = String.Format("{0:X}", number);    // write number back out to TextBox
            textbox.Select(100, 100);
            return number;
        }

        // read a 11bit or 29bit hex number (canbus ID) from TextBox, if not hex reject it, write number back to text box
        private UInt32 read11or29bitHexNumber(TextBox textbox, ref UInt32 number, bool extended)
        {
            if (textbox.Text == "")                      // if TextBox is empty value is 0
                number = 0;
            else                                         // else convert string to hex
                try
                {
                    UInt32 ID = Convert.ToUInt32(textbox.Text, 16);
                    //Console.WriteLine(ID)
                    if (extended)
                    { if (ID <= 0x1FFFFFFF) number = ID; } // if > 29bit reject it
                    else
                        if (ID <= 0x7ffUL) number = ID;   // if > 11bit reject it
                }
                catch (Exception) { }
            //Console.WriteLine("number {0:X}", number);
            textbox.Text = String.Format("{0:X}", number);    // write number back out to TextBox
            textbox.Select(100, 100);
            return number;
        }

        // read a 64bit hex number (canbus ID) from TextBox, if not hex reject it, write number back to text box
        private UInt64 read64bitHexNumber(TextBox textbox, ref UInt64 number)
        {
            if (textbox.Text == "")                      // if TextBox is empty value is 0
                number = 0;
            else                                         // else convert string to hex
                try
                {
                    UInt64 ID = Convert.ToUInt64(textbox.Text, 16);
                    //Console.WriteLine(ID)
                    number = ID;
                }
                catch (Exception) { }
            //Console.WriteLine("number {0:X}", number);
            textbox.Text = String.Format("{0:X}", number);    // write number back out to TextBox
            textbox.Select(100, 100);
            return number;
        }

        // transmit an 8 byte canbus message from canbusMessageTextBox canbus ID is in canbusIDtextBox
        private void CANBUStransmitMessageNow()
        {
            String message = "Tx ";
            transmitCANBUSmessage = false;
            Console.WriteLine("CANBUStransmitMessageNow()");
            if (RTRcheckBox.Checked)         // transmit RTR frame?
            {
                if (extended29bitIDcheckBox.Checked)
                {                                           // transmit RTR frame
                    serialPort1.Write("RE" + canbusIDtextBox.Text + Environment.NewLine);
                    message += "ETX 0x" + canbusIDtextBox.Text + " RTR frame";
                }
                else
                {
                    serialPort1.Write("RS" + canbusIDtextBox.Text + Environment.NewLine);
                    message += "STD ID 0x" + canbusIDtextBox.Text + " RTR frame";
                }
            }
            else
            {                                               // transmit data frame
                if (extended29bitIDcheckBox.Checked)
                {        // transmit extended or standard CanID
                    serialPort1.Write("TE" + canbusIDtextBox.Text + Environment.NewLine);
                    message += "ETX ID 0x" + canbusIDtextBox.Text + " data 0x";
                }
                else
                {
                    serialPort1.Write("TS" + canbusIDtextBox.Text + Environment.NewLine);
                    message += "STD 0x" + canbusIDtextBox.Text + " data 0x";
                }
                message += canbusMessageTextBox.Text;
                serialPort1.Write(canbusMessageTextBox.Text + Environment.NewLine); // transmit data
            }
            terminalTextBox.Invoke(this.myDelegate, message + Environment.NewLine);
            canbusReceivedMessageTextBox.Invoke(this.myDelegate2, message);
            return;
        }


        private void setCanbus_Click(object sender, EventArgs e)
        {
            Console.WriteLine("setCanbus_Click");
            setCANBUSparameters = true;             // set canbus parameters clicked
        }

        private void extended29bitIDcheckBox_CheckedChanged(object sender, EventArgs e)
        {
            canbusIDtextBox.Text = "0";
        }

        // transmit baudrate, mask and filter  to ESP32 monitor
        private void CANBUStransmitSetup()
        {
            Console.WriteLine("CANBUStransmitSetup()");
            setCANBUSparameters = false;
            if (button125.Checked) CANBUSspeed = '1';          // setup canbus speed 
            else
                if (button250.Checked) CANBUSspeed = '2'; else CANBUSspeed = '3';
            Console.WriteLine("CANBUSsetup");
            serialPort1.Write("B" + CANBUSspeed + Environment.NewLine);
            if (RFX0checkBox.Checked)
                serialPort1.Write("ME");
            else serialPort1.Write("MS");
            Console.WriteLine("mask " + CANBUSmaskRFX0textBox.Text + " filter " + CANBUSfilterRXF0_1textBox.Text);
            serialPort1.Write(CANBUSmaskRFX0textBox.Text + Environment.NewLine);
            serialPort1.Write(CANBUSfilterRXF0_1textBox.Text + Environment.NewLine);
        }

        private void clearButton_Click(object sender, EventArgs e)
        {
            terminalTextBox.Clear();        // clear serial data terminal TextBox
        }

        private void label8_Click(object sender, EventArgs e)
        {

        }

        private void canbusTextBox_TextChanged(object sender, EventArgs e)
        {
            read11or29bitHexNumber(canbusIDtextBox, ref canbusTranmitID, extended29bitIDcheckBox.Checked);
            read11or29bitHexNumber(CANBUSmaskRFX0textBox, ref Rx0mask, RFX0checkBox.Checked);
            read11or29bitHexNumber(CANBUSfilterRXF0_1textBox, ref Rx0filter[0], RFX0checkBox.Checked);

        }

        private void RTRcheckBox_CheckedChanged(object sender, EventArgs e)
        {
            if (RTRcheckBox.Checked) canbusMessageTextBox.Enabled = false; else canbusMessageTextBox.Enabled = true;
        }

        private void RFX0checkBox_CheckedChanged(object sender, EventArgs e)
        {
            if (RFX0checkBox.Checked) CANBUSmaskRFX0textBox.Text = "1fffffff";
            else CANBUSmaskRFX0textBox.Text = "7ff";
            CANBUSfilterRXF0_1textBox.Text = "0";
        }
    }
}
