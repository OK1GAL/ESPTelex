#include "header.hpp"

ESPTelnet telnet;

static String telnetBuffer = "";

void onTelnetConnect(String ip)
{
    debug_log("telnet client " + String(ip) + " connected", 1, UART_LOG_MASK | SD_CARD_LOG_MASK);

    telnet.println("\nWelcome to ESPTelex Console (" + telnet.getIP() + ")");
    telnet.println("Type 'help' for available commands. Use 'exit' to disconnect.");
    telnet.print("> ");
    telnetBuffer = "";
}

void onTelnetDisconnect(String ip)
{
    debug_log("telnet client " + String(ip) + " disconnected", 1, UART_LOG_MASK | SD_CARD_LOG_MASK);
    telnetBuffer = "";
}

void onTelnetReconnect(String ip)
{
    debug_log("telnet client " + String(ip) + " reconnected", 1, UART_LOG_MASK | SD_CARD_LOG_MASK);
    telnet.print("> ");
    telnetBuffer = "";
}

void onTelnetConnectionAttempt(String ip)
{
    debug_log("telnet client " + String(ip) + " tried to connect", 1, UART_LOG_MASK | SD_CARD_LOG_MASK);
}

void onTelnetInput(String str)
{
    static bool telnetLastWasCR = false;
    for (size_t i = 0; i < str.length(); i++)
    {
        char c = str[i];
        if (c == '\r')
        {
            telnetLastWasCR = true;
            telnet.println();
            telnetBuffer.trim();
            if (telnetBuffer.length() > 0)
            {
                if (telnetBuffer == "exit")
                {
                    telnet.println("> disconnecting you...");
                    telnet.disconnectClient();
                    telnetBuffer = "";
                    return;
                }
                parseDebugCommands(telnetBuffer);
                telnetBuffer = "";
            }
            if (telnet.isConnected())
            {
                telnet.print("> ");
            }
        }
        else if (c == '\n')
        {
            if (telnetLastWasCR)
            {
                telnetLastWasCR = false;
            }
            else
            {
                telnet.println();
                telnetBuffer.trim();
                if (telnetBuffer.length() > 0)
                {
                    if (telnetBuffer == "exit")
                    {
                        telnet.println("> disconnecting you...");
                        telnet.disconnectClient();
                        telnetBuffer = "";
                        return;
                    }
                    parseDebugCommands(telnetBuffer);
                    telnetBuffer = "";
                }
                if (telnet.isConnected())
                {
                    telnet.print("> ");
                }
            }
        }
        else if (c == '\b' || c == 127) // Backspace / Delete
        {
            telnetLastWasCR = false;
            if (telnetBuffer.length() > 0)
            {
                telnetBuffer.remove(telnetBuffer.length() - 1);
                telnet.print("\b \b"); // Erase character on terminal screen
            }
        }
        else if (c == 3) // Ctrl+C
        {
            telnetLastWasCR = false;
            telnetBuffer = "";
            telnet.println("\r\n> ");
        }
        else if (c >= 32 && c < 127) // Printable character
        {
            telnetLastWasCR = false;
            if (telnetBuffer.length() < 256)
            {
                telnetBuffer += c;
                telnet.print(c); // Smart local echo
            }
        }
    }
}

void init_telnet()
{
    telnet.onConnect(onTelnetConnect);
    telnet.onConnectionAttempt(onTelnetConnectionAttempt);
    telnet.onReconnect(onTelnetReconnect);
    telnet.onDisconnect(onTelnetDisconnect);
    telnet.onInputReceived(onTelnetInput);
    telnet.setLineMode(false); // Character-by-character mode for live echo and backspace
    telnet.begin(TELNET_PORT);
}

void telnet_handler()
{
    telnet.loop();
}
