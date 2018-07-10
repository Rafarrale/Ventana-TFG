#include <EEPROM.h>
#include <WiFiClientSecure.h>
#include <MQTT.h>
#include "esp_system.h"

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

/* DeepSleep timer */
#define uS_TO_S_FACTOR 1000000 /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 60	   /* Time ESP32 will go to sleep (in seconds) */
RTC_DATA_ATTR int estadoAlarmaRTC = 0;

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
static uint16_t timeKeepAlive = 700; // Tiempo que debe pasar para cambiar el estado del dispositivo a desconectado
static uint32_t timeout = 1000;
static int tmeWatchDog = 30000000; //set time in us WATCHDOG
String esid = "";
String cadenaSSID = "";
String cadenaPSK = "";
String estadoAlarma;
char macEsp[10];
String estadoAlarmaTopic;
String mensajeEnvio;
String aux;
long tme;

/* Watchdog */
hw_timer_t *timer = NULL;

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
const char ip[] = "192.168.2.20";
const char pass[] = "BeNq_42?";
const char usuario[] = "usuario1";
const int port = 1883; 
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

WiFiClient net;
MQTTClient client;

void setup()
{
	Serial.begin(115200);
	/* PIN MODES*/
	pinMode(buttonPin, INPUT_PULLDOWN);
	pinMode(led, OUTPUT);
	pinMode(mini_reed_swtich_pin, INPUT_PULLUP);
	pinMode(actmedBateria, OUTPUT);

	// Medicion tiempo DeepSleep reconexion
	esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
	esp_sleep_enable_ext0_wakeup(GPIO_NUM_33, 1); //1 = High, 0 = Low
	Serial.println("Setup ESP32 to sleep for every " + String(TIME_TO_SLEEP) +
				   " Seconds");
	tme = millis();
	/***/

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
		watchDogInit();
	}
	else
	{
		Serial.println("Connecting to Wi-Fi...");
		WiFi.begin(cadenaSSID.c_str(), cadenaPSK.c_str());
		watchDogInit();
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

	Serial.print("estadoAlarmaRTC: ");
	Serial.println(String(estadoAlarmaRTC));
	if (estadoAlarmaRTC)
	{
		store_value = digitalRead(mini_reed_swtich_pin);
		if (store_value)
		{
			mensajeEnvio = "";
			mensajeEnvio = alarma + '#' + esid + '#' + activar + '#';
			Serial.println("Publish: " + mensajeEnvio);
			client.publish(ALARMA, (char *)mensajeEnvio.c_str(), false, 2);
			pasaAlarma = false;
			cerradoAlarma = true;
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
		Serial.println("Publish: " + mensajeEnvio);
		client.publish(ID_REGISTRA, (char *)mensajeEnvio.c_str(), false, 2);
		//Serial.println("Se crea el topic" + esid);
		Serial.println("Subscrito a: " + esid);
		client.subscribe((char *)esid.c_str()); //Reset ID
	}
	else
	{
		/* se manda el mensaje de mi macEsp para pedir un nuevo Id */
		Serial.println("Mandando peticion nuevo Id: ");
		mensajeEnvio = "";
		mensajeEnvio = Nuevo + '#' + String(macEsp) + '#' + tipo + '#' + claveDisp + '#';
		Serial.println("Publish: " + mensajeEnvio);
		client.publish(ID_REGISTRA, (char *)mensajeEnvio.c_str(), false, 2);
	}
	/**/

	Serial.print("setup time is = ");
	tme = millis() - tme;
	Serial.println(tme);
	/***/
}

void loop()
{
	
	client.loop();
	delay(10); // <- fixes some issues with WiFi stability
	if (!client.connected()) {
    	connect();
  	}

	/* Reset SSID y PassWord WiFi */
	pulsacionLarga();
	/* Alarma Contacto */
	readReedRelay();
	/* Actualizamos la bateria cada 20 minutos*/
	readBateria();

	//Serial.print("loop time is = ");
	//tme = millis() - tme;
	//Serial.println(tme);
	
	/* Watchdog, evitamos que se vaya a dormir porque esta activo */
	timerWrite(timer, 0); //reset timer (feed watchdog)
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
		Serial.println("Publish: " + mensajeEnvio);
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
	if (ahora > cuentaReedRelay + 500 && estadoAlarmaRTC)
	{
		cuentaReedRelay = ahora;
		store_value = digitalRead(mini_reed_swtich_pin);
		if (store_value)
		{
			if (pasaAlarma)
			{
				mensajeEnvio = "";
				mensajeEnvio = alarma + '#' + esid + '#' + activar + '#';
				Serial.println("Publish: " + mensajeEnvio);
				client.publish(ALARMA, (char *)mensajeEnvio.c_str(), false, 2);
				pasaAlarma = false;
				cerradoAlarma = true;
			}
		}
		if (!store_value)
		{
			if(!activaAlarma){ // si es falso es que se activo la alarma al despertarse
				/* Entramos en modo deep-sleep */
				Serial.println("Going to sleep now");
				esp_deep_sleep_start();
			}
			if (cerradoAlarma)
			{
				pasaAlarma = true;
				mensajeEnvio = "";
				mensajeEnvio = alarma + '#' + esid + '#' + cerrado + '#';
				Serial.println("Publish: " + mensajeEnvio);
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
	while (!client.connect(macEsp, usuario, pass))
	{

		Serial.print(".");
		delay(1000);
	}

	Serial.println("Connected to MQTT.");

	client.subscribe(macEsp);
	Serial.println("Subscrito a: " + String(macEsp));
	estadoAlarmaTopic = conf_alarma + '/' + macEsp;
	client.subscribe((char *)estadoAlarmaTopic.c_str());
	Serial.println("Subscrito a: " + String(estadoAlarmaTopic));

	/** Mensaje estado de la conexion*/
	mensajeEnvio = "";
	String auxEstadoTopic = estado + '/' + macEsp;
	mensajeEnvio = estado + '#' + macEsp + '#' + constConectado + '#';
	Serial.println("Publish: " + mensajeEnvio);
	client.publish((char *)auxEstadoTopic.c_str(), (char *)mensajeEnvio.c_str(), true, 2);
}

void configuraClientMqtt()
{   
	client.begin(ip, port, net);
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
	WiFi.mode(WIFI_OFF);
	WiFi.mode(WIFI_STA);
	WiFi.begin(cadenaSSID.c_str(), cadenaPSK.c_str());
	configuraClientMqtt();
	connect();
}

void timerEventos()
{
	wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(5000), pdFALSE, (void *)0, reinterpret_cast<TimerCallbackFunction_t>(reconnectWiFi));
	WiFi.onEvent(WiFiEvent);
}

void messageReceived(String &topic, String &payload)
{
	Serial.println("incoming: " + topic + " - " + payload);

	String datosHashtag;
	int hashtag = 0;
	char *cadena;
	aux = "";
	for (int i = 0; i < payload.length(); i++)
	{
		if ((char)payload[i] == '#' || hashtag >= 1)
		{
			hashtag++;
			datosHashtag += (char)payload[i];
		}
		if (hashtag == 0)
		{
			aux += (char)payload[i];
		}
	}

	cadena = (char *)aux.c_str();
	if (strcmp(cadena, (char *)ConstanteElimina.c_str()) == 0)
	{
		/* Pedimos un nuevo Id */
		Serial.println("\r\n Creamos un nuevo Id");
		/*Tipo de dispositivo del nuevo disp*/
		mensajeEnvio = "";
		mensajeEnvio = Nuevo + '#' + String(macEsp) + '#' + tipo + '#' + claveDisp + '#';
		Serial.println("Publish: " + mensajeEnvio);
		client.publish(ID_REGISTRA, (char *)mensajeEnvio.c_str(), false, 2);
	}
	else if (strcmp(cadena, (char *)ConstanteConfirmaDispositivos.c_str()) == 0)
	{
		Serial.println("\r\n Confirmado ID en Dispositivos");
	}

	else if (strcmp(cadena, (char *)ConstanteConfirmaCasa.c_str()) == 0)
	{
		Serial.println("\r\n Confirmado ID en Casa");
		/* Formato mensaje recibido payload = "200#casa" --> datosHashtag = "#casa" */
		String nomCasa;
		for (int i = 0; i < datosHashtag.length(); i++)
		{
			if (datosHashtag[i] != '#')
			{
				nomCasa += datosHashtag[i];
			}
		}
		Serial.print("nomCasa: ");
		Serial.println(nomCasa);
		// Una vez que ya s� que esta en mi casa, pido que me mande el estado de la alarma
		mensajeEnvio = "";
		mensajeEnvio = respAlarma + '#' + nomCasa + '#';
		Serial.println("Publish: " + mensajeEnvio);
		client.publish((char *)respAlarma.c_str(), (char *)mensajeEnvio.c_str(), false, 2);
	}
	else if (strcmp((char *)topic.c_str(), macEsp) == 0)
	{
		Serial.println("\r\n Se recibe ID y se configura el ESP");
		Serial.println("Se unsubscribe " + esid);
		client.unsubscribe((char *)esid.c_str()); //Reset ID
		esid = cadena;
		Serial.println("Subscrito a: " + esid);
		client.subscribe((char *)esid.c_str()); //Reset ID
		Serial.println("writing eeprom ssid:");
		for (int i = MEM_DIR_ID; i < aux.length() + MEM_DIR_ID; ++i)
		{
			EEPROM.writeChar(i, cadena[i]);
			Serial.print("Wrote: ");
			Serial.println(cadena[i]);
		}
		EEPROM.commit();
	}
	else if (strcmp((char *)topic.c_str(), (char *)estadoAlarmaTopic.c_str()) == 0)
	{
		//long tme = millis();
		//Serial.println("running mainloop");
		
		Serial.println("\r\nEstado de la alarma: ");
		Serial.println(aux);
		estadoAlarma = aux;

		Serial.print("Alarm time is = ");
		tme = millis() - tme;
		Serial.println(tme);

		validaestadoAlarma();
		if (activaAlarma)
		{
			estadoAlarmaRTC = 1;
		}
		else
		{
			estadoAlarmaRTC = 0;
		}
		if(!store_value && !activaAlarma || !activaAlarma){ // si es falso es que se activo la alarma al despertarse
			esp_sleep_enable_ext0_wakeup(GPIO_NUM_33, 1); //1 = High, 0 = Low
			esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0); //1 = High, 0 = Low
			/* Entramos en modo deep-sleep */
			Serial.println("Going to sleep now");
			esp_deep_sleep_start();
		}else{
			/* Entramos en modo deep-sleep */
			Serial.println("Going to sleep now");
			esp_deep_sleep_start();
		}
	}
}

void IRAM_ATTR resetModule()
{
	ets_printf("Reiniciando watchdog expired\n");
	esp_restart_noos();
}

void watchDogInit()
{
	/* WatchDog */
	timer = timerBegin(0, 80, true); //timer 0, div 80
	timerAttachInterrupt(timer, &resetModule, true);
	timerAlarmWrite(timer, tmeWatchDog, false);
	timerAlarmEnable(timer); //enable interrupt
}