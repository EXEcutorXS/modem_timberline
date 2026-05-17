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
            uint8_t     A,  // ���������, ������������� ���������� ������� � ���������
                        C,  // �������� �������������� ��������� �� ���������� ����� ���������� ���������  ������.
                        E,  // �������� �������������� ��������� � �������������, ���� ������� ��������� � �������� ������.
                        F,  // ���������� ���� ����� ������:
                            // 0 � �������� � �������������� ������,
                            // 1 � ���������.
                        J,  // ����������� ������ ����� � ���/��� ���.
                        H,  // ���������/���������� ����������� ������������� ��������� �������.
                        I,  // ���������/��������� ����� ����������:
                            // 1 � ���������
                            // 2 � ���������
                            // �� ��������� 2.
                        L,  // ����� ����� ��� SMS (1 � �������, 0 � ����������)
                        N,  // ����������� �������� � ������ ����� ��� ������ � �������� ����������� [80..95]��.
                        P,  // �������� �������������� ������������� �� �������� ��������� �������.
                        R,  // ����������� ��������� ���� [30..60]��. �� ��������� 40��.
                        S,  // ������� ����������� � �������� �������, �� ������� ��������� ����� ���������� ������� ���������� ����� ������������ �� ����������� �������� ��������� � ��������� W. 
                            // �������� ������� ����� �������� � �������� �� 1 �� 30�.
                            // �� ��������� 15.
                        T,  // ����� ������ � �������. ����� ���� ������ � �������� �� 20 �� 120 �����. �� ��������� 40 ���.
                        W,  // ����� �����������:
                            // 0 � ����� ����������� ��������,
                            // 2 � ����� ��������������� ����������� �������,
                            // 3 � ����� ������� ����������� �������.
                            // ���� ����� ������ ���������:
                            // 1 � �� ����������� ����� ����� ������������ ���������
                            // 2 � �� ����������� ������
                            // 3 � �� ����������� �������� (���������) �������
                            // 4 � �� ��������, ���������� � ������ ����������.
                            // �� ��������� 4.
                        e,  // ����� �����������:
                            // 0 � � ������� ������,
                            // 1 � � ����������� ������.
                        p,  // �������� �������� ��������. ���������� � �������� �� 0 (����������� �������� ��������) �� 9 (������������ �������� ��������).
                            // �� ��������� 5.
                        r,  // ������ ����� � ������ ����������� �� ������:
                            // 0 � � ������� ������,  
                            // 1 � ���������.
                        s,  // ���������� ������� ������������:
                            // 0 � � ������� ������, 
                            // 1 � ���������.
                        t;  // ����������� �������� � ������ ����� ��� ������ � �������� ������������� [20..95]��. �� ��������� 88��.
            uint32_t    M;  // ����� ��� ���������� ��������� / ���� ��� 5 ������� ����� � �������, ����� ������� ����� ���������� ������� �������������.
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
        char replyPhone[PHONE_MAX_LEN];  /* phone number for outgoing reply, set by parseMessage() */
        
        
};
extern Sms sms;

#ifdef __cplusplus
extern "C" {
#endif
void sms_emulate(const char* phone, const char* message);
#ifdef __cplusplus
}
#endif

#endif /* __SMS_H */
