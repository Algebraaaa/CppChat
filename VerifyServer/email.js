const nodemailer = require('nodemailer');
const serverConfig = require('./config');

/**
 * 创建发送邮件的代理
 */
const emailTransporter = nodemailer.createTransport({
  host: serverConfig.smtpHost,
  port: serverConfig.smtpPort,
  secure: serverConfig.smtpSecure,
  connectionTimeout: 10000,
  greetingTimeout: 10000,
  socketTimeout: 15000,
  auth: serverConfig.smtpAuth ? {
    user: serverConfig.emailUser, // 发送方邮箱地址
    pass: serverConfig.emailAuthorizationCode, // 邮箱授权码
  } : undefined,
});

emailTransporter.verify(function (verificationError) {
  if (verificationError) {
    console.error('SMTP 连接或认证失败：', verificationError.message);
  } else {
    console.log('SMTP 服务器连接和认证成功');
  }
});

/**
 * 发送邮件的函数
 * @param {*} emailOptions 收件人、主题和正文等邮件参数
 * @returns {Promise<string>} SMTP 服务器返回的发送结果
 */
function sendEmail(emailOptions) {
  return new Promise(function (resolveSend, rejectSend) {
    emailTransporter.sendMail(emailOptions, function (sendError, sendResult) {
      if (sendError) {
        console.error('邮件发送失败：', sendError.message);
        rejectSend(sendError);
      } else {
        console.log('邮件已成功发送：' + sendResult.response);
        resolveSend(sendResult.response);
      }
    });
  });
}

module.exports = { sendEmail };
