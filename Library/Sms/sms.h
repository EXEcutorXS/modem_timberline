/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __SMS_H
#define __SMS_H

/* Includes ------------------------------------------------------------------*/
#include "n32wb452.h"
#include "main.h"
#include <cstdint>

/* Defines ------------------------------------------------------------------ */

/* Classes ------------------------------------------------------------------*/
class Sms
{
    public:
        Sms(void);
        void setFactory(void);
        void processSendSmsEnglish(void);
        void processSendSmsRussian(void);
        void processReadSms(void);
        
        void sendSmsEnglish(const char *number, const char *message);
        void sendSmsRussian(const char *number, const char *message);
        
        bool parseMessage(char* number, char* message);
        bool getMessageParams(char* message);
        
        typedef enum {
            ANSWER_PARAM_NAME_START,
            ANSWER_PARAM_NAME_STOP,
            ANSWER_PARAM_NAME_INFO,
            ANSWER_PARAM_NAME_PARAMETERS
        } AnswerNameTypeDef;
        
        void sendAnswerSmsParameters(AnswerNameTypeDef name);
        void sendAnswerSmsSerialNumber(void);
        void sendAnswerSmsSettings(void);
        void sendAnswerSmsFive(void);
        void sendAnswerSmsList(void);
        void sendAnswerSmsPing(void);
        void sendAnswerText(char *message);
        void SendSetup(void);
        
        typedef enum {
            SMS_CMD_FLAG_A,
            SMS_CMD_FLAG_C,
            SMS_CMD_FLAG_E,
            SMS_CMD_FLAG_F,
            SMS_CMD_FLAG_J,
            SMS_CMD_FLAG_H,
            SMS_CMD_FLAG_I,
            SMS_CMD_FLAG_L,
            SMS_CMD_FLAG_N,
            SMS_CMD_FLAG_P,
            SMS_CMD_FLAG_R,
            SMS_CMD_FLAG_S,
            SMS_CMD_FLAG_T,
            SMS_CMD_FLAG_W,
            SMS_CMD_FLAG_e,
            SMS_CMD_FLAG_p,
            SMS_CMD_FLAG_r,
            SMS_CMD_FLAG_s,
            SMS_CMD_FLAG_t,
            SMS_CMD_FLAG_M
        } SmsCmdFlagsTypeDef;
        uint32_t smsCmdFlags;
        typedef struct sms_cmd_t {
            uint8_t     A,  // добавлено, подтверждение выполнения запуска и остановки
                        C,  // отправка подогревателем сообщения об информации после совершения входящего  вызова.
                        E,  // отправка подогревателем сообщения о неисправности, если таковая возникнет в процессе работы.
                        F,  // управление реле печки салона:
                            // 0 – работает в автоматическом режиме,
                            // 1 – отключено.
                        J,  // управляющий сигнал помпы с ШИМ/без ШИМ.
                        H,  // включение/отключение возможности использования сервисных номеров.
                        I,  // разрешить/запретить режим вентиляции:
                            // 1 – разрешить
                            // 2 – запретить
                            // По умолчанию 2.
                        L,  // выбор языка для SMS (1 – русский, 0 – английский)
                        N,  // температура перехода в ждущий режим при работе в качестве догревателя [80..95]°С.
                        P,  // отправка подогревателем подтверждения об успешном получении команды.
                        R,  // температура включения реле [30..60]°С. По умолчанию 40°С.
                        S,  // Уставка температуры в градусах Цельсия, до которой отопитель будет стремиться нагреть окружающую среду ориентируясь на температуру согласно заданному в параметре W. 
                            // Величина уставки может меняться в пределах от 1 до 30С.
                            // По умолчанию 15.
                        T,  // время работы в минутах. Может быть задано в пределах от 20 до 120 минут. По умолчанию 40 мин.
                        W,  // режим догревателя:
                            // 0 – режим догревателя отключен,
                            // 2 – режим автоматического догревателя включен,
                            // 3 – режим ручного догревателя включен.
                            // либо режим работы отопителя:
                            // 1 – по температуре платы блока электронного отопителя
                            // 2 – по температуре пульта
                            // 3 – по температуре внешнего (кабинного) датчика
                            // 4 – по мощности, задаваемой с пульта управления.
                            // По умолчанию 4.
                        e,  // режим экономичный:
                            // 0 – в обычном режиме,
                            // 1 – в экономичном режиме.
                        p,  // заданное значение мощности. Изменяется в пределах от 0 (минимальное значение мощности) до 9 (максимальное значение мощности).
                            // По умолчанию 5.
                        r,  // работа помпы в режиме догревателя на ждущем:
                            // 0 – в обычном режиме,  
                            // 1 – отключена.
                        s,  // управление каналом сигнализации:
                            // 0 – в обычном режиме, 
                            // 1 – отключена.
                        t;  // температура перехода в ждущий режим при работе в качестве подогревателя [20..95]°С. По умолчанию 88°С.
            uint32_t    M;  // помпа при заведенном двигателе / либо для 5 команды время в минутах, через которое будет произведен запуска подогревателя.
        } sms_cmd_t;
        sms_cmd_t smsCmd;
        
        uint32_t timerSendSms;
        bool isNeedToSendPinSms;
        bool isNeedToSendSmsEnglish;
        bool isNeedToSendSmsRussian;
    
    private:
        #define PHONE_MAX_LEN 16
        #define SMS_MAX_LEN 512

        typedef struct sms_msg_t {
            char phone[PHONE_MAX_LEN];
            char message[SMS_MAX_LEN];
        } sms_msg_t;

        sms_msg_t smsMsg;
        
        
};
extern Sms sms;

#endif /* __SMS_H */
