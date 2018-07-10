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
static const int mini_reed_swtich_pin = 13;
static const int actmedBateria = 17;
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
static int timeKeepAlive = 5; // Tiempo que debe pasar para cambiar el estado del dispositivo a desconectado
String esid = "";
String cadenaSSID = "";
String cadenaPSK = "";
String estadoAlarma;
char macEsp[10];
String estadoAlarmaTopic;
long tme;
long tmeSleep;


/* ReedRelay */
static const char *activar = "I";
static const char *cerrado = "cerrado";

/*MQTT*/
#define MQTT_HOST IPAddress(192, 168, 2, 20)
#define MQTT_PORT 1883
AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer;
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
static const int actBat = 1200000; /* Cambiar aqui el tiempo de actualizacion de la bateria*/
//const int actBat = 10000;

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
		String auxIdRegistra = constIdRegistra + '#' + esid + '#' + macEsp + '#';
		mqttClient.publish(ID_REGISTRA, 2, false, (char *)auxIdRegistra.c_str());
		//Serial.println("Se crea el topic" + esid);
		Serial.println("Subscrito a: " + esid);
		mqttClient.subscribe((char *)esid.c_str(), 2); //Reset ID
	}
	else
	{
		/* se manda el mensaje de mi macEsp para pedir un nuevo Id */
		Serial.println("Mandando peticion nuevo Id: ");
		String aux = Nuevo + '#' + String(macEsp) + '#' + tipo + '#' + claveDisp + '#';
		Serial.print(aux);
		Serial.print(ID_REGISTRA);
		mqttClient.publish(ID_REGISTRA, 2, false, (char *)aux.c_str());
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
	/* Alarma Contacto */
	readReedRelay();
	/* Actualizamos la bateria cada 20 minutos*/
	readBateria();

	long tmePasa = millis() - tmeSleep;
	if(tmePasa > 10000){
		/* Entramos en modo deep-sleep */
		Serial.println("Going to sleep now usually");
		esp_deep_sleep_start();
	}
}
void readBateria()
{
	int ahora = millis();
	if (ahora >= cuentaBateria + actBat)
	{
		cuentaBateria = ahora;
		int aux = parseoBateria();
		String auxRes = Bateria + '#' + esid + '#' + String(aux) + '#';
		Serial.println(auxRes);
		mqttClient.publish((char *)Bateria.c_str(), 2, false, (char *)auxRes.c_str());
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
				String aux = alarma + '#' + esid + '#' + activar + '#';
				mqttClient.publish(ALARMA, 2, false, (char *)aux.c_str());

				pasaAlarma = false;
				cerradoAlarma = true;
			}
		}
		if (!store_value)
		{
			if (cerradoAlarma)
			{
				pasaAlarma = true;
				String aux = alarma + '#' + esid + '#' + cerrado + '#';
				mqttClient.publish(ALARMA, 2, false, (char *)aux.c_str());
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
	mqttClient.setCredentials("usuario1", "BeNq_42?");
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
	Serial.print("Session present: ");
	Serial.println(sessionPresent);

	mqttClient.subscribe(macEsp, 2);
	Serial.println("Subscrito a: " + String(macEsp));
	estadoAlarmaTopic = conf_alarma + '/' + macEsp;
	mqttClient.subscribe((char *)estadoAlarmaTopic.c_str(), 2);
	Serial.println("Subscrito a: " + String(estadoAlarmaTopic));

	/** Mensaje estado de la conexion*/
	String auxEstadoTopic = estado + '/' + macEsp;
	String auxDatosEstado = estado + '#' + macEsp + '#' + constConectado + '#';
	mqttClient.publish((char *)auxEstadoTopic.c_str(), 2, true, (char *)auxDatosEstado.c_str());
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
}

void onMqttUnsubscribe(uint16_t packetId)
{
	Serial.println("Unsubscribe acknowledged.");
	Serial.print("  packetId: ");
	Serial.println(packetId);
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
		String aux = Nuevo + '#' + String(macEsp) + '#' + tipo + '#' + claveDisp + '#';
		mqttClient.publish(ID_REGISTRA, 2, false, (char *)aux.c_str());
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
		String reqAlarma = respAlarma + '#' + nomCasa + '#';
		mqttClient.publish((char *)respAlarma.c_str(), 2, false, (char *)reqAlarma.c_str());
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
	else if (strcmp(topic, (char *)estadoAlarmaTopic.c_str()) == 0)
	{
		Serial.println("\r\nEstado de la alarma: ");
		Serial.println(aux);
		estadoAlarma = aux;
	}
}

void onMqttPublish(uint16_t packetId)
{
	Serial.println("Publish acknowledged.");
	Serial.print("  packetId: ");
	Serial.println(packetId);
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