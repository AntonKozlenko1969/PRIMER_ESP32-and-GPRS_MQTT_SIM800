void GPRS_modem_traffic( void * pvParameters ){
  int _num_index = 0; //счетчик номеров из телефонной книги при записи номеров из массива на СИМ
  bool _AT_ret =false; // возврат от сегмента sendATCommand
  String _comm = String(); //исполняемая команда без AT
  int _povtor = 0; //возможное количество повторов текущей команды
  uint8_t g = 0; // счетчик повторов отправки команды в модем
  uint8_t _interval = 5; // интервал в секундах ожидания ответа от модема
  unsigned long _timeout = 0;        // Переменная для отслеживания таймаута (10 секунд)  
  String _first_com;  // строковая переменная с командой из очереди команд
  _first_com.reserve(max_text_com+1);
//************************************************
  const String apn = F("wap.orange.md"); // vremenno
  const char GPRScomsnt[] PROGMEM = "+SAPBR=";// vremenno
// ********************************************************
uint8_t command_type =0; //тип отправленной в модем команды 
                         // 1 - считать весь список телефонов с СИМ, создать файл PhoneBook.txt с текстом номеров
                         // 2 - создать массив из бинарных значений номеров на СИМ карте
                         // 3 - создать бинарный файл номеров PhoneBook.bin
                         // 4 - скопировать с файла PhoneBookNew.txt все номера на СИМ
                         // 5 - удалить все номера из сим карты
                         // 6 - reset sim800
                         // 7 - GET GPRS запрос
                         // 8 - подключиться к MQTT серверу
                         // 9 - удалить все SMS
                         // 11 - тестовый опрос модема раз в 5 минут
                         // 20 - команда начальной отправки СМС
                         // 30 - одиночная текстовая команда для модема

int8_t _step = 0; //текущий шаг в процедуре GPRS_traffic -глобальная /признак, что процедура занята
    mod_com  modem_comand; //  структура принимающая данные из очереди команд для SIM800

  for (;;){
   _interval = 5; // интервал в секундах ожидания ответа от модема (по умолчанию для всех команд)  
  if (!_AT_ret && _step !=0)   // если предидущая команда неудачно прекратить попытки  
      { _step=14; SMS_currentIndex = 0; // сбросить текущую смс
        _AT_ret=true; 
      if (command_type == 7) retGetZapros(); // если сбой при выполнении GET запроса, закрыть запрос  
      if (command_type == 8) retTCPconnect(); // если сбой при подключении к сайту MQTT, закрыть соединение  
      if (command_type != 7 && command_type != 8)  modemOK = false; 
         }

  // если никакая команда не исполняется и очередь пуста - задача останавливается до появления элементов в очереди
  if (command_type == 0 && _step == 0 ) {
   //mod_com  modem_comand;
   if (xQueueReceive( queue_comand, &modem_comand, portMAX_DELAY) == pdTRUE){
      _first_com = String(modem_comand.text_com);
      command_type = modem_comand.com;  
      flag_modem_resp = modem_comand.com_flag;
      
      if (command_type  == 6 || command_type == 16) IsRestart = false; // признак однократной отправки Restart в модем
      if (command_type  == 11)   IsOpros = false;

     #ifndef NOSERIAL      
        Serial.print("                             Read from QUEUE comand - ");  Serial.print(command_type); 
        Serial.print(" text : "); Serial.println(_first_com);
     #endif     
   }
  }

  if (command_type == 6 || command_type == 16) { //6 Reset SIM800 16 init без рестарта
switch (_step) {
    case 0:
     t_rst = millis();   //Установить интервал, для предотвращения повторного сброса 
     digitalWrite(MODEM_POWER_ON, LOW);
     vTaskDelay(800);

    // Turn on the Modem power first
     digitalWrite(MODEM_POWER_ON, HIGH);

    // Pull down PWRKEY for more than 1 second according to manual requirements
    digitalWrite(MODEM_PWRKEY, HIGH);
    vTaskDelay(100);
    digitalWrite(MODEM_PWRKEY, LOW);
    vTaskDelay(1000);
    digitalWrite(MODEM_PWRKEY, HIGH);

     vTaskDelay(2300); // меньше чем 2,3 секунды модем еще не готов
    t_rst = millis();    
    //   goto EndATCommand; ++_step;
    //   break;    
    // case 0:    
      PIN_ready = false;
      CALL_ready = false;
      GPRS_ready = false; // признак подключения GPRS
      GET_GPRS_OK = false; // признак удачного HTTP GET запроса
      MQTT_connect = false;
      TCP_ready=false;
      modemOK = false; 
      comand_OK = false;    
      _comm=""; _povtor = 1; //AT - Автонастройка скорости
      goto sendATCommand;
      break;
    case 1:
      _comm=F("+CFUN=1,1"); _povtor = 2; //Reset the MT before setting it to <fun> power level.    
      goto sendATCommand;
      break;  
    case 2:
      _comm=F("+CFUN=0"); _povtor = 2; //Set Phone Functionality - Minimum functionality
      goto sendATCommand;
      break;   
    case 3:
      _comm=F("+CIURC=1"); _povtor = 2;// включить отображение состояния
      goto sendATCommand;
      break;  
    case 4:
      _comm=F("+CFUN=1"); _povtor = 2;  // Full functionality (Default)
      //_interval = 25; // ожидает готовность сети интервал в секундах ожидания ответа от модема      
      goto sendATCommand;
      break;   
    case 5:
      _comm=F("E0"); _povtor = 1;       //E0 отлючаем Echo Mode  
      goto sendATCommand;
      break;      
    case 6:  
      _comm=F("+CMGF=1;+CMEE=2"); _povtor = 1;  // Включить TextMode для SMS (0-PDU mode) Задать расширенный ответ при получении ошибки +CMEE=2;
     // _interval = 10; // интервал в секундах ожидания ответа от модема
      goto sendATCommand;
      break;       
    case 7:
      _comm=F("+CPIN?;+CCALR?"); _povtor = 1;// запрос на готовность симки (отсутствие PIN) и готовность звонков +CCALR?
      goto sendATCommand;
      break;      
    case 8:
      _comm=F("+CPBS=\"SM\""); _povtor = 2;// указать место хранения номеров - SIM
      goto sendATCommand;
      break;   
    case 9: 
      _comm=F("+CLIP=1;+CCALR?"); _povtor = 2;// Включаем АОН 
      _interval = 20; // интервал в секундах ожидания ответа от модема
      goto sendATCommand;
      break;    
       // AT+CLTS=0 Отключить вывод текущей временной зоны при каждом входящем звонке
       // AT+CUSD=0 Отключить вывод дополнительной информации при каждом входящем звонке
    case 10:
      _comm=F("+CLTS=0;+CUSD=0"); _povtor = 2;
      goto sendATCommand;
      break;   
   
    case 11:
      _comm=F("&W"); _povtor = 1; //сохраняем значение (AT&W)!      
      goto sendATCommand;
      break;  
    case 12:
      _comm=F("+CCALR?;+CPBS?"); _povtor = 2; // выяснить количество номеров на СИМ
     // _interval = 15; // интервал в секундах ожидания ответа от модема
      goto sendATCommand;
      break;     
    case 13:
  _timeout = millis();             // Переменная для отслеживания таймаута (35 секунд)
  while (!PIN_ready && !CALL_ready && millis()-_timeout<=35000) vTaskDelay(5); // Ждем ответа 35 секунд, если пришел ответ или наступил таймаут, то...   
      if (PIN_ready && CALL_ready){
         modemOK=true;    // модем готов к работе
         #ifndef NOSERIAL   
           Serial.println("                              MODEM OK");               // ... оповещаем об этом и...
         #endif    
       }
      else
      {                                       // Если пришел таймаут, то... модем не готов к работе
        #ifndef NOSERIAL   
          Serial.println("modem Timeout...");               // ... оповещаем об этом и...
        #endif  
       }
     command_type = 0; //не повторять больше
     _step = 0; // дать возможность запросов из вне
     _comm="";
       break; 
    }//end select
  }
  else if (command_type == 9){ // удалить все SMS
     if (_step == 0){
        _step = 13; // создать условие для одноразового прохода
        _comm=F("+CMGDA=\"DEL ALL\""); _povtor = 2;
       goto sendATCommand;
     }
   }
  else if (command_type == 11){ // Тестовая команда, раз в 5 минут
     if (_step == 0){ 
       _comm=""; _povtor = 0; t_rst=millis();
       goto sendATCommand;        
     }
     else if(_step == 1) {
       PIN_ready = false; command_type = 6; CALL_ready = false; 
       _step = 12;// переход к команде сброса, следующий шаг 13 - ожидание ответа PIN READY
        modemOK = false; 
      _comm=F("+CCALR?;+CPIN?"); _povtor = 1;// запрос на готовность симки (отсутствие PIN) и готовность сети
      goto sendATCommand;
     }
   }
  else if (command_type == 30) //выполнить одиночную команду для модема
   { if (_step == 0){ 
       _step = 13;  // создать условие для одноразового прохода
       _comm=_first_com; 
       if (flag_modem_resp == -1) { _povtor = -1; flag_modem_resp=0;} else _povtor = 2;
       goto sendATCommand;
     }
   }
  else if (command_type == 20) //выполнить команду для отправки СМС
   {  
      if (_step == 0){ //выполнить начальную команду для отправки СМС
        flag_modem_resp = 6; // флаг на ожидание приглашения для ввода текста '>'
        _comm = _first_com.substring(0, _first_com.indexOf(charCR)); // сначала передать команду до перевода каретки и дождаться приглашения для ввода текста '>'
         _povtor = -1;
       goto sendATCommand;
      }
     else if (_step == 1){ 
        _step = 13;  // создать условие для одноразового прохода
        _comm = _first_com.substring(_first_com.indexOf(charCR)); // передать текст сообщения после символа перевода каретки
        _povtor = -1;
       _interval = 55; // интервал в секундах ожидания ответа от модема
       goto sendATCommand;
      }  
   }

  else if (command_type == 7) { // GPRS GET
    _interval = 55; // интервал в секундах ожидания ответа от модема  
    switch (_step) {
      case 0: 
        ++_step; goto EndATCommand;//пропустить в рабочем скетче
       _comm=F("+HTTPSTATUS?"); _povtor = 0;        
        goto sendATCommand;        
        break;                 
     case 1:
       _povtor = 1; // установить для всех команд из этой серии
       if (! GPRS_ready) // если модем еще не настроен на GPRS
         { _comm = FPSTR(GPRScomsnt);
           _comm += F("3,1,\"Contype\", \"GPRS\""); 
            goto sendATCommand;
         }
       else
        {_step=5; goto EndATCommand;}
        break;
     case 2:
         _comm = FPSTR(GPRScomsnt);
         _comm += F("3,1,\"APN\", \"");
         _comm += apn +"\"" ; 
        goto sendATCommand;        
        break;  
     case 3:
         _comm=FPSTR(GPRScomsnt);
         _comm += F("1,1"); 
        goto sendATCommand;        
        break; 
     case 4:// проверить подключение и получить GPRS_ready
         _comm = FPSTR(GPRScomsnt);
         _comm += F("2,1"); 
         _step=13; // временно, чтобы перескочить запрос, только установить GPRS соединение
        goto sendATCommand;        
        break; 
    //  case 5: // шаги 5, 6, 7 нужны для HTTP запроса и в данном примере не используются
    //      _comm =F("+HTTPINIT"); 
    //     _comm += colon;
    //     _comm +=F("+HTTPPARA=\"CID\",1"); 
    //     _comm += colon;

    //     _comm += F("+HTTPPARA=\"URL\",\"");
    //     _comm += _first_com; // атрибут адреса из очереди команд
    //     _comm += "\"";

    //     _comm += colon;
    //     _comm +=F("+HTTPACTION=0");
    //     GET_GPRS_OK = false;
    //     goto sendATCommand;        
    //     break; 
    //  case 6: 
    //     if (GET_GPRS_OK)  //если сервер ответил с кодом 200, считать содержимое ответа
    //       {
    //         _comm=F("+HTTPREAD");
    //         goto sendATCommand; }
    //     else 
    //      { 
    //       #ifndef NOSERIAL
    //        Serial.println(" web access fail");
    //       #endif         
    //        _step=7; goto EndATCommand;
    //      }
    //     break; 
      // case 7: 
      //  _comm=F("+HTTPTERM"); 
      //    _step = 13;              
      //   goto sendATCommand;        
      //   break; 

      } // end swith select
  } //end    if comm=7    
  else if (command_type == 8) { // connect to MQTT server
     _interval = 55; // интервал в секундах ожидания ответа от модема  
    switch (_step) {
      case 0: 
      if( TCP_ready) {_step=1; goto EndATCommand;}//признак подключения к MQTT серверу
       _comm  = F("+CIPSTART=\"TCP\",\"");
       _comm +=  _mqttServer; //MQTT_SERVER;
       _comm += F("\",\"");
       _comm.reserve(_comm.length()+8);
       {String st_temp8=String( _mqttPort);
       _comm += st_temp8; } //PORT;
       _comm += F("\""); _povtor = -1;   
        goto sendATCommand;        
        break;      
      case 1: 
        //if (! MQTT_connect) {_step=14; goto EndATCommand;} //признак неудачного TCP подключения 
       _comm  = F("+CIPSEND="); _comm += String(modem_comand.text_com[1] + 2); // отправить определенное количество байт в модем
        _povtor = -1;  
        goto sendATCommand;        
        break;    
      case 2: 
       _step = 13; 
        goto sendATCommand;          
        break;                            
      } // end swith select  
  } // end    if comm=8 
  else if (command_type != 0)  // если тип команды задан, но не обработан - сбросить все значения
   {_step = 0; command_type = 0; _comm="";}

  if (_step > 13) {_step = 0; command_type = 0; _comm="";}  // Максимальное число итераций в команде - 13

sendATCommand:      
 if (command_type != 0) {
  // _AT_ret=false;
// только при отправке текста СМС, после получения приглашения > не добавлять AT в начало команды и '\r' в конце
     if (!((flag_modem_resp == 6 || flag_modem_resp == 8) && _step == 13)) {
      _comm = String(F("AT")) + _comm + String(charCR);  //Добавить в конце командной строки <CR>
       if (command_type==30 && _comm.indexOf(F("ATH")) > -1) one_call = false;
     }
     if (flag_modem_resp != 0) t_last_command = millis(); // засечь время начала исполнения маркированной команды
   g=0;
  do {  // Цикл для организации повторной отправки команды, в случае не удачи 
  comand_OK = false; // при каждой попытке сбросить признак удачного выполнения команды
  if (_step == 13 && (flag_modem_resp == 6 || flag_modem_resp == 8)) { //надо отправить модуль данные после приглашения на ввод '>'
     if (flag_modem_resp == 6) 
        SIM800.write(_comm.c_str());               // Отправляем текст модулю из строки
     else {
       if (flag_modem_resp == 8) {// Отправляем битовый массив модулю
        for (int8_t f=0; f<2; ++f) {// Отправить фиксированный заголовок 2 байта
         SIM800.write(modem_comand.text_com[f]); 
         //Serial.print(modem_comand.text_com[f],HEX); Serial.print(' ');
         if (f==1) {// второй байт это длина остального сообщения которое надо отправить
          for (int r=0; r<modem_comand.text_com[f]; ++r) { 
              SIM800.write(modem_comand.text_com[f+r+1]);  
              //Serial.print(modem_comand.text_com[f+r+1],HEX); Serial.print(' ');
          }
         } 
        }
       }
     }

     //сбросить флаг только при признаке 6 или 8 (нужен чтобы отследить приглашение на ввод данных ">" при отправке СМС или данных в TCP соединение)
      flag_modem_resp = 0;   
  }
  else {  SIM800.write(_comm.c_str());       // Отправляем AT команду модулю из строки
     #ifndef NOSERIAL
       Serial.print("                              Command : ");  Serial.println(_comm);   // Дублируем команду в монитор порта
     #endif  
  }  
  _timeout = millis();     // Переменная для отслеживания таймаута (_interval секунд)
    while (!comand_OK && millis()-_timeout<=_interval * 1000)  // Ждем ответа _interval секунд, если пришел ответ или наступил таймаут, то...  
               vTaskDelay(100);
         _AT_ret=comand_OK;
        #ifndef NOSERIAL           
         if (!_AT_ret){ // Если пришел таймаут, то...
          Serial.println("                              AT Timeout...");               // ... оповещаем об этом и...
          Serial.print("                              _comm= "); Serial.print(_comm);
          Serial.print(" command_type= "); Serial.print(command_type);
          Serial.print(" _step= "); Serial.println(_step);
          }      
        #endif  
        if (g > _povtor) break; // при превышении количества установленных попыток отправки - выйти из цикла
     ++g;  // счетчик попыток отправки текущей команды
   } while ( !comand_OK && ! SIM_fatal_error);   // Не пускать дальше, пока модем не вернет ОК  

   if (_comm.indexOf(F("+HTTPACTION")) > -1){ // ожидание ответа от модема "+HTTPACTION: 0,200"
     g=0;  _povtor= -1; _AT_ret = false;
  do {  
    _timeout = millis();             // Переменная для отслеживания таймаута (6 секунд)
    while (!GET_GPRS_OK && millis()-_timeout<=6000)  // Ждем ответа 6 секунд, если пришел ответ или наступил таймаут, то...  
          vTaskDelay(100);
         _AT_ret=GET_GPRS_OK;
       #ifndef NOSERIAL            
        if (!_AT_ret)  // Если пришел таймаут, то...
          Serial.println("GET_GPRS_OK Timeout...");               // ... оповещаем об этом и...
       #endif                
        if (g > _povtor) break; //  return modemStatus;}
     ++g;
     } while ( !GET_GPRS_OK && ! SIM_fatal_error);   // Не пускать дальше, пока модем не вернет ОК       
   }

   if (_comm.indexOf(F("+CIPSTART")) > -1){ // ожидание ответа от MQTT сервера с удачным подключением CONNECT OK
     g=0; _povtor = -1; _AT_ret = false;
  do {  
    _timeout = millis();             // Переменная для отслеживания таймаута (6 секунд)
    while (! TCP_ready && millis()-_timeout <= 6000)  // Ждем ответа 6 секунд, если пришел ответ или наступил таймаут, то...  
          vTaskDelay(100);
        _AT_ret= TCP_ready;
          #ifndef NOSERIAL           
           if (!_AT_ret)   // Если пришел таймаут, то...
             Serial.println("TCP_ready Timeout...");               // ... оповещаем об этом и...
          #endif 
        if (g > _povtor) break; //  return modemStatus;}
     ++g;
     } while ( ! TCP_ready && ! SIM_fatal_error);   // Не пускать дальше, пока модем не вернет ОК  
   }  
       
    ++_step; //увеличить шаг на 1 для перехода к следующей команде
  } // end ATCommand

  EndATCommand:
    vTaskDelay(1);
  }
}