void parseSMS(const String& msg) {                                   // Парсим SMS
  String msgheader  = "";
  String msgbody    = "";
  String msgphone   = "";
  int firstIndex =msg.indexOf("+CMGR: ");
  //msg = msg.substring(msg.indexOf("+CMGR: "));
  msgheader = msg.substring(firstIndex, msg.indexOf('\r'));           
  #ifndef NOSERIAL
    Serial.println("NEW !!! msgheader: " + msgheader);     
  #endif  
  msgbody = msg.substring(msg.lastIndexOf("\"")+1);  // Выдергиваем текст SMS
  
  msgbody = msgbody.substring(0,30);
  msgbody.trim();

   firstIndex = msgheader.indexOf("\",\"") + 3;
  int secondIndex = msgheader.indexOf("\",\"", firstIndex);
  msgphone = msgheader.substring(firstIndex, secondIndex); // Выдергиваем телефон
  // Записать номер с которого пришло СМС в глобальную переменную для общего доступа
  for (uint8_t j=0; j < msgphone.length()+1; ++j){
      if (j == msgphone.length())
        SMS_incoming_num[j] = NULL;  // если последний символ - добавить нулл в массив для финализации строки
      else  SMS_incoming_num[j] = msgphone[j];
  }
 // получить короткий номер с которого было послано СМС - последние симолы 
  String short_INnumber =String(SMS_incoming_num).substring(String(SMS_incoming_num).length()-(DIGIT_IN_PHONENAMBER-1));
   #ifndef NOSERIAL 
    Serial.print("Phone: "); Serial.println(msgphone);                       // Выводим номер телефона
    Serial.print("Message: " ); Serial.println(msgbody);                      // Выводим текст SMS
    Serial.print("SMS_incoming_num : "); Serial.println(String(SMS_incoming_num)); //.c_str());  // Выводим текст SMS    
    Serial.print("short_INnumber: "); Serial.println(short_INnumber);     
  #endif
// Далее пишем логику обработки SMS-команд.
  // Здесь также можно реализовывать проверку по номеру телефона
  // И если номер некорректный, то просто удалить сообщение.
  
 // Если телефон в белом списке, то...
  if (String(SMS_incoming_num).length() > 6 && whiteListPhones.indexOf(short_INnumber) > -1) {
      msgbody = probel_remove(msgbody);
      madeSMSCommand(msgbody, msgphone);
     }
  else {   // если номер некорректный, то просто удалить сообщение.
   #ifndef NOSERIAL       
    Serial.println("Unknown phonenumber");
   #endif     
    EraseCurrSMS();// Удалить текущую СМС из памяти модуля
    }
}

//выяснение полученой по SMS команды - в примере просто отправка ответа на полученную СМС
void madeSMSCommand(const String& msg, const String& incoming_phone){
          #ifndef NOSERIAL 
             Serial.print("*** mess - "); 
             Serial.print(msg);         
             Serial.print(" incoming phone_num - "); 
             Serial.println(incoming_phone);
          #endif
String SMSResp_Mess =""; 
SMSResp_Mess = F("You send : ");
SMSResp_Mess += msg;
//SMSResp_Mess += charCR + charLF;//"\r\n";
sendSMS(incoming_phone, SMSResp_Mess);
}


// Удалить текущую СМС из памяти модуля
void EraseCurrSMS(){
   // удалить SMS, чтобы не забивали память модуля 
     if (SMS_currentIndex != 0) { // удалить текущую SMS, чтобы не забивали память модуля  
        String  temp_string = "+CMGD=" + String(SMS_currentIndex) + ",0";
          add_in_queue_comand(30, temp_string.c_str(), 0);
          SMS_currentIndex=0;
        #ifndef NOSERIAL        
         Serial.print("EraseCurrSMS = "); Serial.print(SMS_currentIndex);
         Serial.println(" Ready to next SMS from QUEUE");
        #endif          
      }
}

void sendSMS(const String& phone, const String& message){
  //AT+CSCS="GSM"
  String _tempSTR = F("+CMGS=\"");
  _tempSTR += phone;
  _tempSTR += charQuote; 
  _tempSTR += charCR; // F("\r"); //*********!!!!!!!!!!!!!!******************
  _tempSTR += message;
  _tempSTR += (String)((char)26);
   #ifndef NOSERIAL 
    Serial.println("SMS out: " + _tempSTR);
  #endif  
   // 20 - признак отправки СМС 
    add_in_queue_comand(20,_tempSTR.c_str(),0);
}

// Удаление любых пробелов из строки
String probel_remove(const String& msg){
   String temp_resp="";
  for (uint8_t j=0; j < msg.length(); ++j) {
     if (msg[j] != ' ') temp_resp += msg[j];
  }
  return temp_resp; 
}
