
//Добавление нового СМС в очередь на обработку
void add_in_queue_SMS (int _innSMSindex){
  if (xQueueSend(queue_IN_SMS, &_innSMSindex, 0) == pdTRUE){
      #ifndef NOSERIAL      
        Serial.print("Add in QUEUE SMS - "); Serial.println(_innSMSindex);
      #endif       
  }
}

// добавление команды и текста команды в очередь
void add_in_queue_comand(int _inncomand, const char* _inn_text_comand, int _com_flag){
   mod_com modem_comand;

   modem_comand.com = _inncomand;
   modem_comand.com_flag = _com_flag;
   //_inn_text_comand.toCharArray(modem_comand.text_com, _inn_text_comand.length());
   for (int v=0; v<max_text_com; ++v) {
     modem_comand.text_com[v] = _inn_text_comand[v];
     if (_inncomand !=8) {if (_inn_text_comand[v] == NULL) break;}
   }
   bool add_in_queue; // признак добавления команды в очередь
    add_in_queue = xQueueSend(queue_comand, &modem_comand, 0);

   #ifndef NOSERIAL   
    if (add_in_queue == pdTRUE) {
        Serial.print("Add in QUEUE comand - "); Serial.print(_inncomand);
        Serial.print(" text : "); Serial.println(_inn_text_comand);
     }
    else Serial.println("QUEUE is FULL");  
   #endif   
}
