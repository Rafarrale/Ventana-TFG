#include <EEPROM.h>
#include <WiFiClientSecure.h>
#include <MQTT.h>

extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
}

/* ROM */
#define MEM_ID 24
#define MEM_DIR_ID 0
#define MEM_SSID 25
#define MEM_DIR_SSID 24
#define MEM_PSK 25
#define MEM_DIR_PSK 49
#define MEM_TOTAL 74 // Poner aqui la memoria total que se debe reservar para guardar los datos

//const GPIO
static const int buttonPin = 19;
static const int led = 5;
static const int mini_reed_swtich_pin = 13;
static const int actmedBateria = 32;
static const int analogMedBateria = 36;

//var
static bool pasa = true;
static bool pasaAlarma = true;
static bool cerradoAlarma = true;
static bool activaAlarma = false;
static int buttonState = 0;
static int resetWiFi = 20;
static int store_value = 0;
static int memValuesWifi = 25;
static int cuentaReedRelay = 0;
static int cuenta = 0;
static int cuentaBateria = 0;
static int timeKeepAlive = 10; // Tiempo que debe pasar para cambiar el estado del dispositivo a desconectado
static int timeout = 1000;
String esid = "";
String cadenaSSID = "";
String cadenaPSK = "";
String estadoAlarma;
char macEsp[10];
String estadoAlarmaTopic;
String mensajeEnvio;

/* ReedRelay */
static const char *activar = "I";
static const char *cerrado = "cerrado";

/*MQTT*/
TimerHandle_t wifiReconnectTimer;

/* topics */
#define ID_REGISTRA "idRegistra"
#define ALARMA "alarma"

/* Constantes */
static char msgId[MEM_ID];
static const String ConstanteElimina = "elimina";
static const String Nuevo = "nuevo";
static const String Bateria = "bateria";
static const String armar = "armar";
static const String desarmar = "desarmar";
static const String alarma = "alarma";
static const String casa = "casa";
static const String respAlarma = "respAlarma";
static const String estado = "estado";
static const String conf_alarma = "confAlarma";
static const char *constDesconectado = "desconectado";
static const char *constConectado = "conectado";
static const String constIdRegistra = "idRegistra";
static const String tipo = "contacto";		 /* Cambiar aqui el tipo de dispositivo para su registro  */
static const String claveDisp = "159753456"; /* Cambiar aqui la clave del dispositivo */
static const String ConstanteConfirmaCasa = "200";
static const String ConstanteConfirmaDispositivos = "201";
static const String validaSsidPsk = "*";
static const String encripta = "encripta";
static const int actBat = 1200000; /* Cambiar aqui el tiempo de actualizacion de la bateria*/

WiFiClientSecure net;
MQTTClient client;

const char *ca_cert = \ 
"-----BEGIN CERTIFICATE-----\n"
					  "MIIDqjCCApKgAwIBAgIJAI/DEk3GiiIYMA0GCSqGSIb3DQEBDQUAMGoxFzAVBgNV\n"
					  "BAMMDkFuIE1RVFQgYnJva2VyMRYwFAYDVQQKDA1Pd25UcmFja3Mub3JnMRQwEgYD\n"
					  "VQQLDAtnZW5lcmF0ZS1DQTEhMB8GCSqGSIb3DQEJARYSbm9ib2R5QGV4YW1wbGUu\n"
					  "bmV0MB4XDTE4MDcwMjEwMjMxN1oXDTMyMDYyODEwMjMxN1owajEXMBUGA1UEAwwO\n"
					  "QW4gTVFUVCBicm9rZXIxFjAUBgNVBAoMDU93blRyYWNrcy5vcmcxFDASBgNVBAsM\n"
					  "C2dlbmVyYXRlLUNBMSEwHwYJKoZIhvcNAQkBFhJub2JvZHlAZXhhbXBsZS5uZXQw\n"
					  "ggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQC/q4jgXF6UnrAGKUl07XL2\n"
					  "wg4PXNPbsH9B2poxqT4sRB4lLt3mlK4yDEATq2j9H5+PajY/cGoBl4UZJI6aUExP\n"
					  "J+0gAwW4EM/OOUTPxOHe2Yf+ky6PIIOLTRMYss5mgASYt+CBr2Q4b68aHWKbQv8d\n"
					  "F+zRnhiSGyceP1v3g32uHMWzwDl+geRTrIXwYkWXc/94gkMgookTcN+h+KWr0UnR\n"
					  "VQEKJoj6DY3s5Z2xn9IQ7Lj2GGqaPpDZqS+6yYRJnAqDdiw+xK8yO8j0xFk5KIWC\n"
					  "yzk98ixZRrT3GRCt4JvX6/+V/rAQcI6DW81dPtn5OiywaQvW3TLUdAF+ec1DhMqV\n"
					  "AgMBAAGjUzBRMB0GA1UdDgQWBBSWQUyfpwP5edxmeBPCGURv+eIAODAfBgNVHSME\n"
					  "GDAWgBSWQUyfpwP5edxmeBPCGURv+eIAODAPBgNVHRMBAf8EBTADAQH/MA0GCSqG\n"
					  "SIb3DQEBDQUAA4IBAQADyoim7d1qKEOGJipSBiXWgVuLKpcAfLeOQYf+ldY+A2po\n"
					  "QalrGBHkrk2vqa+dpHUdRQ4Zl7SbBdVX9lOXEDXE0DQ+KPiRGGfgLm9kTvZHuG8v\n"
					  "a4FImR8QRNoi/Y5fwVhp7KlfMCw1nZRhXYq990pL/ENarJKTw0ufaT3kc1/PHmaE\n"
					  "x27rADY/LfcvaBSn+F38pReRvASYY3ppNsS7HT3Xtygv3Pu4s0Htz4Ua9zoBXkIB\n"
					  "VWmfenTh+osdZVLEJj/PW5a/xiQ0GjCw3i9el6vVfxnxUDIIjrYqUuYC9YzRuNK8\n"
					  "oQ3axUnAzZfEbyv4i866hHPjlb1NwlO8c17R8/Hv\n"
					  "-----END CERTIFICATE-----\n";

void setup()
{
	Serial.begin(115200);
	/* PIN MODES*/
	pinMode(buttonPin, INPUT_PULLDOWN);
	pinMode(led, OUTPUT);
	pinMode(mini_reed_swtich_pin, INPUT_PULLUP);
	pinMode(actmedBateria, OUTPUT);

	/* EEPROM */
	EEPROM.begin(MEM_TOTAL);
	sprintf(macEsp, "%d", ESP.getEfuseMac()); //Obtnemos la MAC del ESP como identificador unico
											  //---------------------------
											  //READ to eeprom
	Serial.println("Reading EEPROM SSID ");
	for (int i = MEM_DIR_SSID; i < MEM_DIR_SSID + MEM_SSID; ++i)
	{
		cadenaSSID += char(EEPROM.readChar(i));
	}
	//Serial.println(cadenaSSID.length());
	Serial.print("SSID: ");
	Serial.println(cadenaSSID);

	//READ to eeprom
	Serial.println("Reading EEPROM PSK");
	for (int i = MEM_DIR_PSK; i < MEM_DIR_PSK + MEM_PSK; ++i)
	{
		cadenaPSK += char(EEPROM.readChar(i));
	}
	//Serial.println(cadenaPSK.length());
	Serial.print("PSK: ");
	Serial.println(cadenaPSK);
	//---------------------------
	/*COMPRUEBA si es SSID o PSK correcto*/
	int validaSSID = 0;
	int validaPsk = 0;
	String aux;
	char *cadAuxSSID;
	for (int i = 0; i < MEM_SSID; i++)
	{
		aux += cadenaSSID[i];
		cadAuxSSID = (char *)aux.c_str();
		//Serial.println(isAlphaNumeric(aux.charAt(0)));
		if (isAlphaNumeric(aux.charAt(0)) == 0)
		{
			validaSSID++;
		}
		aux = "";
	}
	char *cadAuxPsk;
	for (int i = 0; i < MEM_PSK; i++)
	{
		aux += cadenaPSK[i];
		cadAuxPsk = (char *)aux.c_str();
		//Serial.println(isAlphaNumeric(aux.charAt(0)));
		if (isAlphaNumeric(aux.charAt(0)) == 0)
		{
			validaPsk++;
		}
		aux = "";
	}

	if (validaSSID == memValuesWifi && validaPsk == memValuesWifi)
	{
		/* SMARTCONFIG*/
		//Init WiFi as Station, start SmartConfig
		WiFi.mode(WIFI_AP_STA);
		WiFi.beginSmartConfig();
		//Wait for SmartConfig packet from mobile
		Serial.println("Waiting for SmartConfig.");
		while (!WiFi.smartConfigDone())
		{
			delay(500);
			Serial.print(".");
		}

		Serial.println("");
		Serial.println("SmartConfig received.");
	}
	else
	{
		Serial.println("Connecting to Wi-Fi...");
		WiFi.begin(cadenaSSID.c_str(), cadenaPSK.c_str());
	}

	timerEventos();
	configuraClientMqtt();
	connect();

	if (validaSSID == memValuesWifi && validaPsk == memValuesWifi)
	{
		//---------------------------
		/* Write SSID and PSK to EEPROM*/
		cadenaSSID = WiFi.SSID();
		Serial.println(cadenaSSID);
		cadenaPSK = WiFi.psk();
		Serial.println(cadenaPSK);
		int auxDir = MEM_DIR_SSID;
		for (int i = 0; i < MEM_SSID; ++i)
		{
			EEPROM.writeChar(auxDir, cadenaSSID[i]);
			auxDir++;
			Serial.print("Wrote: ");
			Serial.println(cadenaSSID[i]);
		}
		EEPROM.commit();
		auxDir = MEM_DIR_PSK;
		for (int i = 0; i < MEM_PSK; ++i)
		{
			EEPROM.writeChar(auxDir, cadenaPSK[i]);
			auxDir++;
			Serial.print("Wrote: ");
			Serial.println(cadenaPSK[i]);
		}
		EEPROM.commit();
		//---------------------------
	}

	/*EEPROM*/
	esid = "";
	//READ to eeprom
	Serial.println("Reading EEPROM ID ");
	for (int i = MEM_DIR_ID; i < MEM_ID; ++i)
	{
		esid += char(EEPROM.readChar(i));
	}
	//Serial.println(esid.length());
	Serial.print("ID: ");
	Serial.println(esid);

	int digAux = esid.length();
	int digComp = 0;
	for (int i = 0; i < esid.length(); i++)
	{
		char aux = esid[i];
		if (isAlphaNumeric(aux) != 0)
		{
			digComp = digComp + 1;
		}
	}

	/* Si el valor leido de memoria en el inicio es valido, se comprueba si existe en la casa
	en caso de que exista se recibe confirmacion, sino, se elimina de la BBDD*/
	if (digComp == digAux)
	{
		/* publish the message */
		//**Mensaje ==> idRegistra#esid#*macEsp#/
		mensajeEnvio = "";
		mensajeEnvio = constIdRegistra + '#' + esid + '#' + macEsp + '#';
		client.publish(ID_REGISTRA, (char *)mensajeEnvio.c_str(), false, 2);
		//Serial.println("Se crea el topic" + esid);
		Serial.println("Subscrito a: " + esid);
		client.subscribe((char *)esid.c_str(), 2); //Reset ID
	}
	else
	{
		/* se manda el mensaje de mi macEsp para pedir un nuevo Id */
		Serial.println("Mandando peticion nuevo Id: ");
		mensajeEnvio = "";
		mensajeEnvio = Nuevo + '#' + String(macEsp) + '#' + tipo + '#' + claveDisp + '#';
		client.publish(ID_REGISTRA, (char *)mensajeEnvio.c_str(), false, 2);
	}
	/**/
}

void loop()
{
	client.loop();
	delay(10); // <- fixes some issues with WiFi stability

	if (!client.connected())
	{
		connect();
	}

	/* Reset SSID y PassWord WiFi */
	pulsacionLarga();
	/* Alarma Contacto */
	readReedRelay();
	/* Actualizamos la bateria cada 20 minutos*/
	readBateria();
}

void readBateria()
{
	int ahora = millis();
	if (ahora >= cuentaBateria + actBat)
	{
		cuentaBateria = ahora;
		int aux = parseoBateria();
		mensajeEnvio = "";
		mensajeEnvio = Bateria + '#' + esid + '#' + String(aux) + '#';
		client.publish((char *)Bateria.c_str(), (char *)mensajeEnvio.c_str(), false, 2);
	}
}

int parseoBateria()
{
	/*
	1 = Activamos el transistor BJT (Pin 17) para que se normalice la tension de entrada al pin analogico
	2 = Podemos hacer la lectura del valor desde el pin analogico (2)
	3 = Dejamos de leer el pin
	4 = Desactivamos el transistor BJT para que deje de consumir corriente el divisor de tension
	*/

	digitalWrite(actmedBateria, HIGH);
	double aux = 0.0;
	double valor = 4095.0;
	for (int i = 0; i < 10; i++)
	{
		aux = ReadVoltage(analogMedBateria);
		if (aux < valor)
		{
			valor = aux;
		}
	}
	digitalWrite(actmedBateria, LOW);

	int res = 0;
	// Hasta 3.8V
	if (valor <= 4095.0 && valor >= 1.75)
	{
		Serial.println(valor);
		res = 30;
	}
	// Hasta 3.45V
	else if (valor <= 1.75 && valor >= 1.61)
	{
		Serial.println(valor);
		res = 10;
	}
	// Hasta 3.3V
	else if (valor <= 1.61 && valor >= 0.5)
	{
		Serial.println(valor);
		res = 5;
	}
	else
	{
		Serial.println(valor);
		res = 0;
	}
	return res;
}

double ReadVoltage(byte pin)
{
	double reading = analogRead(pin); // Reference voltage is 3v3 so maximum reading is 3v3 = 4095 in range 0 to 4095
	if (reading < 1 || reading > 4095)
		return 0;
	return -0.000000000000016 * pow(reading, 4) + 0.000000000118171 * pow(reading, 3) - 0.000000301211691 * pow(reading, 2) + 0.001109019271794 * reading + 0.034143524634089;
}

void readReedRelay()
{
	int ahora = millis();
	if (ahora > cuentaReedRelay + 500 && validaestadoAlarma())
	{
		cuentaReedRelay = ahora;
		store_value = digitalRead(mini_reed_swtich_pin);
		if (store_value)
		{
			if (pasaAlarma)
			{
				mensajeEnvio = "";
				mensajeEnvio = alarma + '#' + esid + '#' + activar + '#';
				Serial.println(mensajeEnvio);
				client.publish(ALARMA, (char *)mensajeEnvio.c_str(), false, 2);
				pasaAlarma = false;
				cerradoAlarma = true;
			}
		}
		if (!store_value)
		{
			if (cerradoAlarma)
			{
				pasaAlarma = true;
				mensajeEnvio = "";
				mensajeEnvio = alarma + '#' + esid + '#' + cerrado + '#';
				Serial.println(mensajeEnvio);
				client.publish(ALARMA, (char *)mensajeEnvio.c_str(), false, 2);
				cerradoAlarma = false;
			}
		}
	}
}

boolean validaestadoAlarma()
{

	if (estadoAlarma == armar || estadoAlarma == casa)
	{
		activaAlarma = true;
	}
	else if (estadoAlarma == desarmar)
	{
		activaAlarma = false;
	}
	return activaAlarma;
}

void pulsacionLarga()
{
	int ahora = millis();
	buttonState = digitalRead(buttonPin);
	if (buttonState == HIGH)
	{
		if (pasa)
		{
			cuenta = ahora;
			pasa = false;
		}
		if (ahora > cuenta + 5000 && digitalRead(led) == HIGH)
		{
			resetWiFiSsid();
			parpadeaLedBloqueante(250, led);
			ESP.restart();
		}
	}
	else
	{
		pasa = true;
		digitalWrite(led, HIGH);
	}
}

void resetWiFiSsid()
{
	cadenaSSID = "";
	for (int i = 0; i < MEM_SSID; i++)
	{
		cadenaSSID += "*";
	}
	cadenaPSK = "";
	for (int i = 0; i < MEM_PSK; i++)
	{
		cadenaPSK += "*";
	}
	int auxDir = MEM_DIR_SSID;
	for (int i = 0; i < MEM_SSID; ++i)
	{
		EEPROM.writeChar(auxDir, cadenaSSID[i]);
		auxDir++;
		Serial.print("Wrote: ");
		Serial.println(cadenaSSID[i]);
	}
	EEPROM.commit();
	auxDir = MEM_DIR_PSK;
	for (int i = 0; i < MEM_PSK; ++i)
	{
		EEPROM.writeChar(auxDir, cadenaPSK[i]);
		auxDir++;
		Serial.print("Wrote: ");
		Serial.println(cadenaPSK[i]);
	}
	EEPROM.commit();
}

void parpadeaLedBloqueante(int time, int led)
{
	for (int i = 0; i <= 10; i++)
	{
		digitalWrite(led, LOW);
		delay(time);
		digitalWrite(led, HIGH);
		delay(time);
	}
}

void connect()
{
	//Wait for WiFi to connect to AP
	int auxResetWifi = 0;
	int buttonResetWiFi = 0;
	Serial.println("Waiting for WiFi");
	while (WiFi.status() != WL_CONNECTED)
	{
		delay(500);
		Serial.print(".");
		auxResetWifi++;
		buttonResetWiFi = digitalRead(buttonPin);
		if (auxResetWifi == resetWiFi && buttonResetWiFi == HIGH)
		{
			Serial.println("reset wifi");
			resetWiFiSsid();
			parpadeaLedBloqueante(250, led);
			ESP.restart();
		}
	}

	Serial.print("\nconnecting to MQTT...");
	while (!client.connect(macEsp, "usuario1", "BeNq_42?")) //TODO: usuario en constantes y meter el connect en while
	{

		Serial.print(".");
		delay(1000);
	}

	Serial.println("Connected to MQTT.");

	client.subscribe(macEsp, 2);
	Serial.println("Subscrito a: " + String(macEsp));
	estadoAlarmaTopic = conf_alarma + '/' + macEsp;
	client.subscribe((char *)estadoAlarmaTopic.c_str(), 2);
	Serial.println("Subscrito a: " + String(estadoAlarmaTopic));

	/** Mensaje estado de la conexion*/
	mensajeEnvio = "";
	String auxEstadoTopic = estado + '/' + macEsp;
	mensajeEnvio = estado + '#' + macEsp + '#' + constConectado + '#';
	client.publish((char *)auxEstadoTopic.c_str(), (char *)mensajeEnvio.c_str(), true, 2);
}

void WiFiEvent(WiFiEvent_t event)
{
	Serial.printf("[WiFi-event] event: %d\n", event);
	switch (event)
	{
	case SYSTEM_EVENT_STA_GOT_IP:
		Serial.println("WiFi connected");
		Serial.println("IP address: ");
		Serial.println(WiFi.localIP());
		break;
	case SYSTEM_EVENT_STA_DISCONNECTED:
		Serial.println("WiFi lost connection");
		xTimerStart(wifiReconnectTimer, 0);
		break;
	case SYSTEM_EVENT_STA_LOST_IP:
		Serial.println("station lost IP");
		ESP.restart();
		break;
	}
}

void reconnectWiFi()
{
	Serial.println("reconectando WiFi");
	WiFi.reconnect();
}

void timerEventos()
{
	wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(5000), pdFALSE, (void *)0, reinterpret_cast<TimerCallbackFunction_t>(reconnectWiFi));
	WiFi.onEvent(WiFiEvent);
}

void messageReceived(String &topic, String &payload)
{
	Serial.println("incoming: " + topic + " - " + payload);
}
void configuraClientMqtt()
{
	net.setCACert(ca_cert); //Estableemos el certificado de la autoridad CA para comprobar que es nuestro servidor
	client.begin("192.168.2.20", 8883, net);
	client.onMessage(messageReceived);
	client.setOptions(timeKeepAlive, false, timeout); //Se mantiene la sesion
	/** Mensaje de aviso si se pierde la conexion*/
	// TOPIC
	String topicEstadoDisp = estado + '/' + macEsp;
	char *aux = new char[topicEstadoDisp.length() + 1];
	strcpy(aux, topicEstadoDisp.c_str());
	// MENSAJE
	mensajeEnvio = "";
	mensajeEnvio = estado + '#' + macEsp + '#' + constDesconectado + '#';
	char *auxDatos = new char[mensajeEnvio.length() + 1];
	strcpy(auxDatos, mensajeEnvio.c_str());
	client.setWill(aux, auxDatos, true, 2);
}
