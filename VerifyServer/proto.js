const path = require('path');
const grpc = require('@grpc/grpc-js');
const protoLoader = require('@grpc/proto-loader');

const VERIFY_PROTO_FILE_PATH = path.join(__dirname, 'message.proto');
const loadedProtoDefinition = protoLoader.loadSync(VERIFY_PROTO_FILE_PATH, {
  keepCase: true,
  longs: String,
  enums: String,
  defaults: true,
  oneofs: true,
});
const loadedGrpcPackages = grpc.loadPackageDefinition(loadedProtoDefinition);

// messagePackage 对应 message.proto 中的 package message。
const messagePackage = loadedGrpcPackages.message;

module.exports = messagePackage;
