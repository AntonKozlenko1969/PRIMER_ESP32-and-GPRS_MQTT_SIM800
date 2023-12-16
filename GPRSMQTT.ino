void mqttCallback(char* topic, byte* payload, unsigned int length) {
   #ifndef NOSERIAL
     Serial.print(F("MQTT message arrived ["));
     Serial.print(topic);
     Serial.print(F("] "));
    for (int i = 0; i < length; ++i)  Serial.print((char)payload[i]); 
    Serial.println();
  #endif

String _mqttTopic = FPSTR(mqttRelayTopic);
  _mqttTopic +=FPSTR(mqttRelayConfigTopic);
  char* topicBody = topic + _mqttClient.length();
  //strncmp(str1, str2, num) Сравнивает первые num символов из строк str1 и str2. Возвращает 0, если эти участки одинаковы.
  if (! strncmp(topicBody, _mqttTopic.c_str(), _mqttTopic.length())) { 
    topicBody += _mqttTopic.length();
    if (*topicBody++ == charSlash) {
      int8_t id = 0;

      while ((*topicBody >= '0') && (*topicBody <= '9')) {
        id *= 10;
        id += *topicBody++ - '0';
      }
      if ((id > 0) && (id <= maxRelays)) {
        id--;
        switch ((char)payload[0]) {
          case '0':
            switchRelay(id, false);
            break;
          case '1':
            switchRelay(id, true);
            break;
          default:
            if (relayPin[id] != -1) {
              bool relay = digitalRead(relayPin[id]);
              //if (! relayLevel[id])
                relay = ! relay;
              //mqttPublish(String(topic), String(relay));
            }
        }
      } else {
         #ifndef NOSERIAL 
          Serial.println(F("Wrong relay index!"));
         #endif 
      }
    } else {
        #ifndef NOSERIAL
         Serial.println(F("Unexpected topic!"));
        #endif 
    }
  } else {
      #ifndef NOSERIAL
        Serial.println(F("Unexpected topic!"));
      #endif  
  }
}

void mqttResubscribe() {
  String topic;
  topic = _mqttClient;  
  topic += FPSTR(mqttRelayTopic);
  topic += FPSTR(mqttRelayConfirmTopic);
  topic += charSlash;

  for (int8_t i = 0; i < maxRelays; i++) {
    if (relayPin[i] != -1) {
      mqttPublish(topic + String(i + 1), String(digitalRead(relayPin[i])));
    }  
  }  

    topic = _mqttClient;
    topic += FPSTR(mqttRelayTopic);    
    topic +=FPSTR(mqttRelayConfigTopic);
    topic += F("/#");
    mqttSubscribe(topic);

}

bool mqttSubscribe(const String& topic) {
  #ifndef NOSERIAL
    Serial.print(F("MQTT subscribe to topic \""));
    Serial.print(topic);
    Serial.println('\"');
  #endif
  if (MQTT_connect) {
     GPRS_MQTT_sub(topic); 
    return true;
  } 
}

bool mqttPublish(const String& topic, const String& value) {
  #ifndef NOSERIAL
    Serial.print(F("MQTT publish topic \""));
    Serial.print(topic);
    Serial.print(F("\" with value \""));
    Serial.print(value);
    Serial.println('\"');
  #endif  
  if (MQTT_connect) {
     GPRS_MQTT_pub(topic, value); 
    return true;
  }
}

void GPRS_MQTT_Reconnect(){
  static uint32_t timeout; //  = 30000;
  static uint32_t nextTime;
  static bool resub; // признак, что переподключение и переподпика прошла успешно
  static uint8_t connect_attempt; // количество попыток подключения к MQTT серверу
  // шаг в процессе подключения, для исключения передачи в очередь одинаковых команд. 
  // С момента ее первой подачи, до момента ее передачи в модем и исполнения.
  static uint8_t reconnect_step; 
  
   if ((int32_t)(millis() - nextTime) >= 0) {
       
        #ifndef NOSERIAL  
         Serial.print("reconnect_step = ");  
         Serial.print(reconnect_step);
         Serial.print(" connect_attempt = ");  
         Serial.print(connect_attempt);  
         Serial.print(" resub = "); Serial.print(resub ? F(" TRUE ") : F(" FALSE "));
         Serial.print(" GPRS_ready = "); Serial.print(GPRS_ready ? F(" TRUE ") : F(" FALSE "));          
         Serial.print(" TCP_ready = "); Serial.print(TCP_ready ? F(" TRUE ") : F(" FALSE "));
         Serial.print(" MQTT_connect = "); Serial.println(MQTT_connect ? F(" TRUE ") : F(" FALSE "));            
       #endif 

   if( !GPRS_ready && reconnect_step == 0) { // признак подключения GPRS
       add_in_queue_comand(7,"", 0); //включить режим GPRS 
      reconnect_step = 1; timeout = 5000; resub=false; return;  // Не подавать следующую команду пока не подключимся
      }
   if (!GPRS_ready && reconnect_step > 0) {++connect_attempt; resub=false;}  
   if (GPRS_ready && reconnect_step == 0) {++reconnect_step;  resub=false;} 
   if(!TCP_ready && GPRS_ready && reconnect_step == 1) {//признак подключения к MQTT серверу
         GPRS_MQTT_connect (); reconnect_step = 2; timeout = 800; ++connect_attempt; resub=false; return; // Не подавать следующую команду пока не подключимся
      }
   if (reconnect_step > 1) {
      if (MQTT_connect) {
        connect_attempt=0;
        if (!resub) {
           String topic ;
           //topic += charSlash;// 21/11/2023          
           topic = _mqttClient;   
           topic += mqttDeviceStatusTopic;    
           mqttPublish(topic, mqttDeviceStatusOn);   
           topic =_mqttClient; 
           topic += FPSTR(mqttDeviceIPTopic);  
           String String_IP;
               String_IP = F("GPRS");//IPAddress2String(WiFi.localIP());
           mqttPublish(topic, String_IP);  // добавлено для отображения на MQTT локально IP адреса            
           mqttResubscribe(); 
           resub=true;
          }
         GPRS_MQTT_ping(); //только поддержать соединение          
         reconnect_step = 7; timeout = 52000;
       }
      else { ++reconnect_step; }  
     }

    // если подключились к MQTT серверу, но сервер скинул подключение (не верный пользователь или пароль)
    if (reconnect_step > 9) {reconnect_step=0; timeout = 30000; resub=false;}//создать условие для нового прохода подключений через 20 * timeout
    // если пытаемся подключиться, но сервер вообще не отвечает (сервер не доступен, не верный адрес, URL)
    if (connect_attempt > 6) timeout = 3*60*1000; // пробовать через 3 минуты
    if (connect_attempt > 15) timeout = 7*60*1000; // пробовать через 7 минут
    if (connect_attempt == 20) {timeout = 30*1000; reconnect_step=0; connect_attempt=0; resub=false;} // начать попытки заново
    if (!modemOK) {reconnect_step=0; timeout = 30000; resub=false;}//создать условие для нового прохода подключений
    
   nextTime = millis() + timeout;  
  }

}

void GPRS_MQTT_connect (){
  char _inn_comm[max_text_com];
  int _curr_poz = 2; // текущая позиция в массиве
  uint16_t rest_length =0; // общее количество байт в пакете (кроме первых двух)
  String topic ;
         topic += _mqttClient;   
         topic += mqttDeviceStatusTopic;         

    //  #ifndef NOSERIAL  
    //     Serial.print("connect topic / mess ");     
    //     Serial.print(topic); 
    //     Serial.print(" / ");         
    //     Serial.println(String(mqttDeviceStatusOff));         
    //   #endif 

  _inn_comm[0] = 0x10; //#0 идентификатор пакета на соединение
  // оставшееся количество байт без логина и пароля пользователя
  //  пример: _inn_comm[1] = strlen(MQTT_type)+_mqttClient.length()+_mqttUser.length()+_mqttPassword.length()+topic.length()+strlen(mqttDeviceStatusOff)+16;   
  rest_length = strlen(MQTT_type)+_mqttClient.length()+topic.length()+strlen(mqttDeviceStatusOff)+12; 
  _inn_comm[_curr_poz] =0x00; ++_curr_poz; //#2 старший байт длины названия протокола
  _inn_comm[_curr_poz] =strlen(MQTT_type); ++_curr_poz; //#3 младший байт длины названия протокола
  for (int v=0;v<strlen(MQTT_type);++v) {_inn_comm[_curr_poz] = MQTT_type[v]; ++_curr_poz;} //байты типа протокола  
  _inn_comm[_curr_poz] =0x04; ++_curr_poz; // Protocol Level byte 00000100
  //Connect Flag bits: 7-User Name Flag, 6-Password Flag, 5-Will Retain, 4-Will QoS, 3-Will QoS, 2-Will Flag, 1-Clean Session, 0-Reserved
  if (_mqttClient == strEmpty) {
     _inn_comm[_curr_poz] =0x2E; ++_curr_poz;  //Connect Flag bits без логина и пароля 0x2E 00101110, 0x2C 00101100
  }   
  else {
     _inn_comm[_curr_poz] =0xEE; ++_curr_poz;  //Connect Flag bits с логином и паролем 0xEE 11101110, 0xEC 11101100
     rest_length += _mqttUser.length()+_mqttPassword.length()+4;
  }
  _inn_comm[1] = rest_length;  // оставшееся количество байт без логина и пароля пользователя
  _inn_comm[_curr_poz] =0x00; ++_curr_poz; _inn_comm[_curr_poz] =0x3C; ++_curr_poz; // время жизни сессии (2 байта) 0x23-35sec, 0x28-40sec, 0x3C-60sec
  _inn_comm[_curr_poz] =0x00; ++_curr_poz; _inn_comm[_curr_poz] =_mqttClient.length(); ++_curr_poz; // длина идентификатора (2 байта)
  for (int v=0;v<_mqttClient.length();++v) {_inn_comm[_curr_poz] = _mqttClient[v]; ++_curr_poz;}  // MQTT  идентификатор устройства
  _inn_comm[_curr_poz] =0x00; ++_curr_poz; _inn_comm[_curr_poz]=topic.length(); ++_curr_poz;  // длина LWT топика (2 байта) 
  for (int v=0;v<topic.length();++v) {_inn_comm[_curr_poz] = topic[v]; ++_curr_poz;}  // LWT топик    
  _inn_comm[_curr_poz] =0x00; ++_curr_poz; _inn_comm[_curr_poz]=strlen(mqttDeviceStatusOff); ++_curr_poz; // длина LWT сообщения (2 байта) 
  for (int v=0;v<strlen(mqttDeviceStatusOff);++v) {_inn_comm[_curr_poz] = mqttDeviceStatusOff[v]; ++_curr_poz;}   // LWT сообщение  

  if (_mqttClient != strEmpty) {
      _inn_comm[_curr_poz] =0x00; ++_curr_poz; _inn_comm[_curr_poz]=_mqttUser.length(); ++_curr_poz;// длина MQTT логина (2 байта) 
     for (int v=0;v<_mqttUser.length();++v) {_inn_comm[_curr_poz] = _mqttUser[v]; ++_curr_poz;} // MQTT логин
     _inn_comm[_curr_poz] =0x00; ++_curr_poz; _inn_comm[_curr_poz]=_mqttPassword.length(); ++_curr_poz;// длина MQTT пароля (2 байта) 
     for (int v=0;v<_mqttPassword.length();++v) {_inn_comm[_curr_poz] = _mqttPassword[v]; ++_curr_poz;} // MQTT пароль
  }
  add_in_queue_comand(30, String(F("+GSMBUSY=1")).c_str(), 0);
  add_in_queue_comand(8, _inn_comm, 8);
  add_in_queue_comand(30, String(F("+GSMBUSY=0")).c_str(), 0);  
}

 void GPRS_MQTT_pub (const String& _topic, const String& _messege) {          // пакет на публикацию
  char _inn_comm[max_text_com];
  int _curr_poz = 4; // текущая позиция в массиве

    //  #ifndef NOSERIAL  
    //     Serial.print("pub topic / mess ");     
    //     Serial.print(_topic); 
    //     Serial.print(" / ");         
    //     Serial.println(_messege);         
    //   #endif 

    _inn_comm[0]=0x33; // было 0x30 без retain Qos0, 0x31 с retain Qos0, 0x33 Qos1 (не работает??)
    _inn_comm[1]=_topic.length()+_messege.length()+2+2; //отсавшаяся длина пакета
    _inn_comm[2]=0x00; _inn_comm[3]=_topic.length();
    for (int8_t v=0; v<_topic.length();++v) {_inn_comm[_curr_poz]=_topic[v]; ++_curr_poz;}// топик
    _inn_comm[_curr_poz]=0x00; ++_curr_poz; _inn_comm[_curr_poz]=0x10; ++_curr_poz; // идентификатор отправленного пакета для подтвержения публикации
    for (int8_t v=0; v<_messege.length();++v) {_inn_comm[_curr_poz]=_messege[v]; ++_curr_poz;}   // сообщение  

  add_in_queue_comand(30, String(F("+GSMBUSY=1")).c_str(), 0);
  add_in_queue_comand(8, _inn_comm, 8);
  add_in_queue_comand(30, String(F("+GSMBUSY=0")).c_str(), 0);  
  }                                                 

void GPRS_MQTT_ping () {                                // пакет пинга MQTT сервера для поддержания соединения
  char _inn_comm[max_text_com];
  _inn_comm[0]=0xC0; _inn_comm[1]=0x00;

  add_in_queue_comand(30, String(F("+GSMBUSY=1")).c_str(), 0);
  add_in_queue_comand(8, _inn_comm, 8);
  add_in_queue_comand(30, String(F("+GSMBUSY=0")).c_str(), 0);   
}

 void GPRS_MQTT_sub (const String& _topic) {                                       // пакет подписки на топик
  char _inn_comm[max_text_com];
  int _curr_poz = 6; // текущая позиция в массиве
      //  #ifndef NOSERIAL  
      //   Serial.print("sub topic ");     
      //   Serial.println(_topic); 
      // #endif    

  _inn_comm[0]=0x82; 
  _inn_comm[1]=_topic.length()+5;   // сумма пакета 
  _inn_comm[2]=0x00; _inn_comm[3]=0x01; _inn_comm[4]=0x00;
  _inn_comm[5]=_topic.length();  // топик
    for (int8_t v=0; v<_topic.length();++v) {_inn_comm[_curr_poz]=_topic[v]; ++_curr_poz;}  
  _inn_comm[_curr_poz]=0x00;   

  add_in_queue_comand(30, String(F("+GSMBUSY=1")).c_str(), 0);
  add_in_queue_comand(8, _inn_comm, 8);
  add_in_queue_comand(30, String(F("+GSMBUSY=0")).c_str(), 0);  
   } 