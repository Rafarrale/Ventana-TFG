#include <WiFi.h>
#include <EEPROM.h>
extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
}
#include <AsyncMqttClient.h>

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
static const int interruptor = 13;

/* DeepSleep timer */
#define uS_TO_S_FACTOR 1000000 /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 60	   /* Time ESP32 will go to sleep (in seconds) */

//var
static bool pasa = true;
static bool activaInterruptor = false;
static int buttonState = 0;
static int resetWiFi = 20;
static int store_value = 0;
static int memValuesWifi = 25;
static int cuenta = 0;
static int timeKeepAlive = 70;	 // Tiempo que debe pasar para cambiar el estado del dispositivo a desconectado
static int tmeWatchDog = 30000000; //set time in us WATCHDOG
String esid = "";
String cadenaSSID = "";
String cadenaPSK = "";
String estadoInterruptor;
char macEsp[10];
String estadoInterruptorTopic;
long tme;
long tmeSleep;
static long tmeSleepDiferencia = 2000;
static bool watchMqtt = true;  // Variable usada para prevenir bloqueos con conexion mqtt
String mensajeEnvio;
String aux;

/* Watchdog */
hw_timer_t *timer = NULL;

/*MQTT*/
#define MQTT_HOST IPAddress(192, 168, 2, 20)
#define MQTT_PORT 1883
AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer;
TimerHandle_t wifiReconnectTimer;

/* topics */
#define ID_REGISTRA "idRegistra"

/* Constantes */
static const String tipo = "interruptor";	/* Cambiar aqui el tipo de dispositivo para su registro  */
static const String claveDisp = "159753456"; /* Cambiar aqui la clave del dispositivo */
static char msgId[MEM_ID];
static const String ConstanteElimina = "elimina";
static const String Nuevo = "nuevo";
static const String activar = "activar";
static const String desactivar = "desactivar";
static const String respInterruptor = "respInterruptor";
static const String estado = "estado";
static const String conf_interruptor = "confInterruptor";
static const char *constDesconectado = "desconectado";
static const char *constConectado = "conectado";
static const String constIdRegistra = "idRegistra";
static const String ConstanteConfirmaCasa = "200";
static const String ConstanteConfirmaDispositivos = "201";
static const String validaSsidPsk = "*";
static const char* constTrue = "true";
static const char* constFalse = "false";

void setup()
{
	Serial.begin(115200);
	/* PIN MODES*/
	pinMode(buttonPin, INPUT_PULLDOWN);
	pinMode(led, OUTPUT);
	pinMode(interruptor, OUTPUT);

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
		watchDogInit();
		connectToWifi();
	}

	timerEventos();

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
		Serial.println("Publish: " + mensajeEnvio);
		mqttClient.publish(ID_REGISTRA, 2, false, (char *)mensajeEnvio.c_str());
		//Serial.println("Se crea el topic" + esid);
		Serial.println("Subscrito a: " + esid);
		mqttClient.subscribe((char *)esid.c_str(), 2); //Reset ID
	}
	else
	{
		/* se manda el mensaje de mi macEsp para pedir un nuevo Id */
		Serial.println("Mandando peticion nuevo Id: ");
		mensajeEnvio = "";
		mensajeEnvio = Nuevo + '#' + String(macEsp) + '#' + tipo + '#' + claveDisp + '#';
		Serial.print(aux);
		Serial.println("Publish: " + mensajeEnvio);
		mqttClient.publish(ID_REGISTRA, 2, false, (char *)mensajeEnvio.c_str());
	}
	/**/

	Serial.print("setup time is = ");
	tme = millis() - tme;
	Serial.println(tme);

	// Inicio de tiempo para intervalo sin datos en recepcion para sleep
	tmeSleep = millis();
}

void loop()
{
	/* Reset SSID y PassWord WiFi */
	pulsacionLarga();

	/* Activa y desactiva pin interruptor*/
	confDisp();

	if(watchMqtt){
		/* Watchdog, evitamos que se vaya a dormir porque esta activo */
		timerWrite(timer, 0); //reset timer (feed watchdog)
	}

	//Serial.print("loop time is = ");
	//tme = millis() - tme;
	//Serial.println(tme);
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
		if (ahora > cuenta + 2000 && digitalRead(led) == HIGH)
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
void connectToWifi()
{
	Serial.println("Connecting to Wi-Fi...");
	WiFi.begin(cadenaSSID.c_str(), cadenaPSK.c_str());
}
void connectToMqtt()
{
	Serial.println("Connecting to MQTT...");
	mqttClient.setClientId(macEsp);
	mqttClient.setCleanSession(false);
	mqttClient.setKeepAlive(timeKeepAlive);
	/** Mensaje de aviso si se pierde la conexion*/
	// TOPIC
	String topicEstadoDisp = estado + '/' + macEsp;
	char *aux = new char[topicEstadoDisp.length() + 1];
	strcpy(aux, topicEstadoDisp.c_str());
	// MENSAJE
	String datosEstadoDisp = estado + '#' + macEsp + '#' + constDesconectado + '#';
	char *auxDatos = new char[datosEstadoDisp.length() + 1];
	strcpy(auxDatos, datosEstadoDisp.c_str());
	mqttClient.setWill(aux, 2, true, auxDatos);
	mqttClient.connect();
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
		connectToMqtt();
		break;
	case SYSTEM_EVENT_STA_DISCONNECTED:
		Serial.println("WiFi lost connection");
		watchMqtt = false;
		xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
		xTimerStart(wifiReconnectTimer, 0);
		break;
	case SYSTEM_EVENT_STA_LOST_IP:
		Serial.println("station lost IP");
		ESP.restart();
		break;
	}
}
void onMqttConnect(bool sessionPresent)
{
	Serial.println("Connected to MQTT.");
	watchMqtt = true;
	Serial.print("Session present: ");
	Serial.println(sessionPresent);

	mqttClient.subscribe(macEsp, 2);
	Serial.println("Subscrito a: " + String(macEsp));
	estadoInterruptorTopic = conf_interruptor + '/' + macEsp;
	mqttClient.subscribe((char *)estadoInterruptorTopic.c_str(), 2);
	Serial.println("Subscrito a: " + String(estadoInterruptorTopic));

void reconnectWiFi()
{
	Serial.println("reconectando WiFi");
	WiFi.disconnect(true);
	WiFi.begin(cadenaSSID.c_str(), cadenaPSK.c_str());
}

void timerEventos()
{
	mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(5000), pdFALSE, (void *)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
	wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(5000), pdFALSE, (void *)0, reinterpret_cast<TimerCallbackFunction_t>(connectToWifi));
	WiFi.onEvent(WiFiEvent);

	mqttClient.onConnect(onMqttConnect);
	mqttClient.onDisconnect(onMqttDisconnect);
	mqttClient.onSubscribe(onMqttSubscribe);
	mqttClient.onUnsubscribe(onMqttUnsubscribe);
	mqttClient.onMessage(onMqttMessage);
	mqttClient.onPublish(onMqttPublish);
	mqttClient.setServer(MQTT_HOST, MQTT_PORT);
}

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t length, size_t index, size_t total)
{
	Serial.print("\r\nMessage received: ");
	Serial.println(topic);

	tmeSleep = millis();

	String aux;
	String datosHashtag;
	int hashtag = 0;
	char *cadena;
	Serial.print("payload: ");
	for (int i = 0; i < length; i++)
	{
		Serial.print((char)payload[i]);
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
		mqttClient.publish(ID_REGISTRA, 2, false, (char *)mensajeEnvio.c_str());
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
		// Una vez que ya s� que esta en mi casa, pido que me mande el estado del interruptor
		mensajeEnvio = "";
		mensajeEnvio = respInterruptor + '#' + nomCasa + '#' + esid + '#';
		Serial.println("Publish: " + mensajeEnvio);
		mqttClient.publish((char *)respInterruptor.c_str(), 2, false, (char *)mensajeEnvio.c_str());

		/** Mensaje estado de la conexion*/
		mensajeEnvio = "";
		String auxEstadoTopic = estado + '/' + macEsp;
		mensajeEnvio = estado + '#' + macEsp + '#' + constConectado + '#';
		Serial.println("Publish: " + mensajeEnvio);
		mqttClient.publish((char *)auxEstadoTopic.c_str(), 2 ,true, (char *)mensajeEnvio.c_str());
}
	}
	else if (strcmp(topic, macEsp) == 0)
	{
		Serial.println("\r\n Se recibe ID y se configura el ESP");
		Serial.println("Se unsubscribe " + esid);
		mqttClient.unsubscribe((char *)esid.c_str()); //Reset ID
		esid = cadena;
		Serial.println("Subscrito a: " + esid);
		mqttClient.subscribe((char *)esid.c_str(), 2); //Reset ID
		Serial.println("writing eeprom ssid:");
		for (int i = MEM_DIR_ID; i < aux.length() + MEM_DIR_ID; ++i)
		{
			EEPROM.writeChar(i, cadena[i]);
			Serial.print("Wrote: ");
			Serial.println(cadena[i]);
		}
		EEPROM.commit();
	}
	else if (strcmp(topic, (char *)estadoInterruptorTopic.c_str()) == 0)
	{
		Serial.println("\r\nEstado del interruptor: ");
		Serial.println(aux);
		estadoInterruptor = aux;
	}
}

void IRAM_ATTR resetModule()
{
	ets_printf("WatchDog trigger\n");
	esp_restart_noos();
}

void confDisp()
{
	validaestadoInterruptor();
	digitalWrite(interruptor, activaInterruptor);
}

boolean validaestadoInterruptor()
{

	if (strcmp((char*)estadoInterruptor.c_str(), constTrue) == 0)
	{
		activaInterruptor = true;
	}
	else if (strcmp((char*)estadoInterruptor.c_str(), constFalse) == 0)
	{
		activaInterruptor = false;
	}
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
	Serial.println("Disconnected from MQTT.");
	if (WiFi.isConnected())
	{
		xTimerStart(mqttReconnectTimer, 0);
	}
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos)
{
	Serial.println("Subscribe acknowledged.");
	Serial.print("  packetId: ");
	Serial.println(packetId);
	Serial.print("  qos: ");
	Serial.println(qos);
	tmeSleep = millis();
}

void onMqttUnsubscribe(uint16_t packetId)
{
	Serial.println("Unsubscribe acknowledged.");
	Serial.print("  packetId: ");
	Serial.println(packetId);
	tmeSleep = millis();
}

void onMqttPublish(uint16_t packetId)
{
	Serial.println("Publish acknowledged.");
	Serial.print("  packetId: ");
	Serial.println(packetId);
	tmeSleep = millis();
}

void watchDogInit()
{
	/* WatchDog */
	timer = timerBegin(0, 80, true); //timer 0, div 80
	timerAttachInterrupt(timer, &resetModule, true);
	timerAlarmWrite(timer, tmeWatchDog, false);
	timerAlarmEnable(timer); //enable interrupt
}