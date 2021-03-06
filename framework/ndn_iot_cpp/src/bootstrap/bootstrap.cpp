#include <time.h>

#include "bootstrap.hpp"
#include "boost-info-parser.hpp"
#include "app-request.pb.h"
#include <ndn-cpp/encoding/protobuf-tlv.hpp>

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

using namespace ndn;
using namespace std;
using namespace ndn::func_lib;

namespace ndn_iot {

Bootstrap::Bootstrap
  (ndn::Face& face)
 : face_(face), certificateContentCache_(&face)
{
  identityStorage_ = ptr_lib::shared_ptr<BasicIdentityStorage>(new BasicIdentityStorage());
  filePrivateKeyStorage_ = ptr_lib::shared_ptr<FilePrivateKeyStorage>(new FilePrivateKeyStorage());

  certificateCache_ = ptr_lib::shared_ptr<CertificateCache>(new CertificateCache());
  policyManager_ = ptr_lib::shared_ptr<ConfigPolicyManager>(new ConfigPolicyManager("", certificateCache_));
  policyManager_->load(" \n \
    validator            \n \
    {                    \n \
      rule               \n \
      {                  \n \
        id \"initial rule\" \n \
        for data            \n \
        checker             \n \
        {                   \n \
          type hierarchical \n \
        }                \n \
      }                  \n \
    }", "initial-rule");
  identityManager_ = ptr_lib::shared_ptr<IdentityManager>(new IdentityManager(identityStorage_, filePrivateKeyStorage_));
  keyChain_.reset(new KeyChain(identityManager_, policyManager_));
  keyChain_->setFace(&face_);
  defaultCertificateName_ = Name();
}



Bootstrap::~Bootstrap()
{
}

/**
 * Initial keyChain and defaultCertificate setup
 */
ptr_lib::shared_ptr<KeyChain>
Bootstrap::setupDefaultIdentityAndRoot
  (Name defaultIdentity, Name signerName)
{
  if (defaultIdentity.size() == 0) {
    try {
      defaultIdentity = identityManager_->getDefaultIdentity();
    } catch (const SecurityException& e) {
      cout << "Default identity does not exist " << e.what() << endl;
      throw std::runtime_error("Default identity does not exist\n");
    }
  }
  try {
    defaultIdentity_ = Name(defaultIdentity);
    defaultCertificateName_ = identityManager_->getDefaultCertificateNameForIdentity(defaultIdentity_);
    defaultKeyName_ = identityManager_->getDefaultKeyNameForIdentity(defaultIdentity_);
  } catch (const SecurityException& e) {
    cout << "Cannot find keys for configured identity " << defaultIdentity_.toUri() << endl;
    throw std::runtime_error("Cannot find keys for configured identity\n");
  }
  face_.setCommandSigningInfo(*keyChain_, defaultCertificateName_);
  certificateContentCache_.registerPrefix(Name(defaultCertificateName_).getPrefix(-1), 
    bind(&Bootstrap::onRegisterFailed, this, _1));
  
  ptr_lib::shared_ptr<Data> myCertificate = keyChain_->getCertificate(defaultCertificateName_);
  certificateContentCache_.add(*myCertificate);
  Name actualSignerName = (KeyLocator::getFromSignature(myCertificate->getSignature())).getKeyName();

  if (signerName.size() != 0 && actualSignerName != signerName) {
    cout << "Signer name mismatch" << endl;
    throw std::runtime_error("Signer name mismatch\n");
  }

  controllerName_ = getIdentityNameFromCertName(actualSignerName);
  try {
    controllerCertificate_ = keyChain_->getCertificate(identityManager_->getDefaultCertificateNameForIdentity(controllerName_));
    certificateCache_->insertCertificate(*controllerCertificate_);
  } catch (const SecurityException& e) {
    cout << "Default certificate for controller identity does not exist " << e.what() << endl;
    throw std::runtime_error("Default certificate for controller identity does not exist\n");
  }
  return keyChain_;
}

Name
Bootstrap::getDefaultCertificateName()
{
  return defaultCertificateName_;
}

Name
Bootstrap::getDefaultIdentity()
{
  return defaultIdentity_;
}

/**
 * Handling application producing authorization
 */
void
Bootstrap::requestProducerAuthorization(Name dataPrefix, std::string appName, OnRequestSuccess onRequestSuccess, OnRequestFailed onRequestFailed)
{
  if (defaultCertificateName_.size() == 0) {
    throw std::runtime_error("Default certificate is missing! Try setupDefaultIdentityAndRoot first?");
  }
  sendAppRequest(defaultCertificateName_, dataPrefix, appName, onRequestSuccess, onRequestFailed);
}

void
Bootstrap::sendAppRequest
(Name certificateName, Name dataPrefix, std::string appName, OnRequestSuccess onRequestSuccess, OnRequestFailed onRequestFailed) {
  AppRequestMessage message;
  int i = 0;

  for (i = 0; i < certificateName.size(); i++) {
    message.mutable_command()->mutable_idname()->add_components(certificateName.get(i).toEscapedString());
  }
      
  for (i = 0; i < dataPrefix.size(); i++) {
    message.mutable_command()->mutable_dataprefix()->add_components(dataPrefix.get(i).toEscapedString());
  }
  
  message.mutable_command()->set_appname(appName);
  
  Name requestInterestName(Name(controllerName_).append("requests").append(Name::Component(ProtobufTlv::encode(message))));
  
  Interest requestInterest(requestInterestName);
  // TODO: change this. (for now, make this request long lived (100s), if the controller operator took some time to respond)
  requestInterest.setInterestLifetimeMilliseconds(4000);
  // don't use keyChain.sign(interest), since it's of a different format from commandInterest
  face_.makeCommandInterest(requestInterest);
  
  int appRequestTimeoutCnt = 3;
  
  face_.expressInterest
    (requestInterest, 
     bind(&Bootstrap::onAppRequestData, this, _1, _2, onRequestSuccess, onRequestFailed), 
     bind(&Bootstrap::onAppRequestTimeout, this, _1, onRequestSuccess, onRequestFailed, appRequestTimeoutCnt), 
     bind(&Bootstrap::onNetworkNack, this, _1, _2, onRequestSuccess, onRequestFailed));
  cout << "Application publish request sent: " + requestInterest.getName().toUri() << endl;
  
  return ;
}

void
Bootstrap::onAppRequestDataVerified
(const ptr_lib::shared_ptr<Data>& data, OnRequestSuccess onRequestSuccess, OnRequestFailed onRequestFailed)
{
  string content = "";
  for (size_t i = 0; i < data->getContent().size(); ++i) {
    content += (*data->getContent())[i];
  }
  rapidjson::Document d;
  d.Parse(content.c_str());

  rapidjson::Value& s = d["status"];
  if (s.GetInt() == 200) {
    onRequestSuccess();
  } else {
    onRequestFailed("Application request failed with message: " + content);
  }
}

void
Bootstrap::onAppRequestDataVerifyFailed
(const ptr_lib::shared_ptr<Data>& data, string reason, OnRequestSuccess onRequestSuccess, OnRequestFailed onRequestFailed)
{
  onRequestFailed("Application request response verification failed: " + reason);
}

void
Bootstrap::onAppRequestData
(const ptr_lib::shared_ptr<const Interest>& interest, const ptr_lib::shared_ptr<Data>& data, OnRequestSuccess onRequestSuccess, OnRequestFailed onRequestFailed)
{
  string content = "";
  for (size_t i = 0; i < data->getContent().size(); ++i) {
    content += (*data->getContent())[i];
  }

  keyChain_->verifyData(data, 
    bind(&Bootstrap::onAppRequestDataVerified, this, _1, onRequestSuccess, onRequestFailed), 
    (const OnDataValidationFailed)bind(&Bootstrap::onAppRequestDataVerifyFailed, this, _1, _2, onRequestSuccess, onRequestFailed));
  return;
}

void 
Bootstrap::onAppRequestTimeout
(const ptr_lib::shared_ptr<const Interest>& interest, OnRequestSuccess onRequestSuccess, OnRequestFailed onRequestFailed, int appRequestTimeoutCnt)
{
  if (appRequestTimeoutCnt == 0) {
    onRequestFailed("Application request failed because of interest time out");
  } else {
    Interest newInterest(*interest);
    newInterest.refreshNonce();
    face_.expressInterest(newInterest, 
      bind(&Bootstrap::onAppRequestData, this, _1, _2, onRequestSuccess, onRequestFailed), 
      bind(&Bootstrap::onAppRequestTimeout, this, _1, onRequestSuccess, onRequestFailed, appRequestTimeoutCnt - 1), 
      bind(&Bootstrap::onNetworkNack, this, _1, _2, onRequestSuccess, onRequestFailed));
  }
  return;
}

void
Bootstrap::onNetworkNack
(const ptr_lib::shared_ptr<const Interest>& interest, const ptr_lib::shared_ptr<NetworkNack>& networkNack, OnRequestSuccess onRequestSuccess, OnRequestFailed onRequestFailed)
{
  onRequestFailed("Application request failed because of interest NACK");
  return;
}

/**
 * Handling application consumption (trust schema update)
 */
void
Bootstrap::startTrustSchemaUpdate
(Name appPrefix, OnUpdateSuccess onUpdateSuccess, OnUpdateFailed onUpdateFailed)
{
  std::string appNamespace = appPrefix.toUri();
  std::map<std::string, ptr_lib::shared_ptr<AppTrustSchema>>::iterator it = trustSchemas_.find(appNamespace);
  if (it != trustSchemas_.end()) {
    if (it->second->getFollowing()) {
      cout << "Already following this trust schema" << endl;
      return;
    }
    it->second->setFollowing(true);
  } else {
    trustSchemas_[appNamespace].reset(new AppTrustSchema(true, "", 0, true));
  }

  Interest initialInterest(Name(appNamespace).append("_schema"));
  initialInterest.setChildSelector(1);
  face_.expressInterest(initialInterest, 
    bind(&Bootstrap::onTrustSchemaData, this, _1, _2, onUpdateSuccess, onUpdateFailed),
    bind(&Bootstrap::onTrustSchemaTimeout, this, _1, onUpdateSuccess, onUpdateFailed),
    bind(&Bootstrap::onNetworkNackSchema, this, _1, _2, onUpdateSuccess, onUpdateFailed));
}

void
Bootstrap::stopTrustSchemaUpdate()
{
  cout << "Stop trust schema update not implemented" << endl;
}

void
Bootstrap::onTrustSchemaData
(const ptr_lib::shared_ptr<const Interest>& interest, const ptr_lib::shared_ptr<Data>& data, OnUpdateSuccess onUpdateSuccess, OnUpdateFailed onUpdateFailed)
{
  if (!controllerCertificate_) {
    cout << "Unexpected: controller certificate not present" << endl;
  } else {
    cout << certificateCache_->getCertificate(controllerCertificate_->getName().getPrefix(-1))->getName().toUri() << endl;
    cout << data->getName().toUri() << endl;
    keyChain_->verifyData(data, 
      bind(&Bootstrap::onSchemaVerified, this, _1, onUpdateSuccess, onUpdateFailed),
      (const OnDataValidationFailed)bind(&Bootstrap::onSchemaVerificationFailed, this, _1, _2, onUpdateSuccess, onUpdateFailed));
  }
  return;
}

void
Bootstrap::onTrustSchemaTimeout
(const ptr_lib::shared_ptr<const Interest>& interest, OnUpdateSuccess onUpdateSuccess, OnUpdateFailed onUpdateFailed)
{
  Interest newInterest(*interest);
  newInterest.refreshNonce();
  face_.expressInterest(newInterest, 
    bind(&Bootstrap::onTrustSchemaData, this, _1, _2, onUpdateSuccess, onUpdateFailed),
    bind(&Bootstrap::onTrustSchemaTimeout, this, _1, onUpdateSuccess, onUpdateFailed),
    bind(&Bootstrap::onNetworkNackSchema, this, _1, _2, onUpdateSuccess, onUpdateFailed));
}

void
Bootstrap::onSchemaVerified
(const ptr_lib::shared_ptr<const Data>& data, OnUpdateSuccess onUpdateSuccess, OnUpdateFailed onUpdateFailed)
{
  Name::Component version = data->getName().get(-1);
  std::string appNamespace = data->getName().getPrefix(-2).toUri();
  
  std::map<std::string, ptr_lib::shared_ptr<AppTrustSchema>>::iterator it = trustSchemas_.find(appNamespace);
  if (it == trustSchemas_.end()) {
    cout << "unexpected: received trust schema for application namespace that's not being followed; malformed data name?" << endl;
    return;
  }

  if (version.toVersion() < it->second->getVersion()) {
    cout << "Got out of date schema" << endl;
    onUpdateFailed("Got out of date schema");
    return;
  }

  it->second->setVersion(version.toVersion());
  string trustSchema = "";
  for (size_t i = 0; i < data->getContent().size(); ++i) {
    trustSchema += (*data->getContent())[i];
  }
  it->second->setSchema(trustSchema);

  Interest newInterest(Name(data->getName()).getPrefix(-1));
  newInterest.setChildSelector(1);
  Exclude exclude;
  exclude.appendAny();
  exclude.appendComponent(version);
  newInterest.setExclude(exclude);

  face_.expressInterest(newInterest, 
    bind(&Bootstrap::onTrustSchemaData, this, _1, _2, onUpdateSuccess, onUpdateFailed),
    bind(&Bootstrap::onTrustSchemaTimeout, this, _1, onUpdateSuccess, onUpdateFailed),
    bind(&Bootstrap::onNetworkNackSchema, this, _1, _2, onUpdateSuccess, onUpdateFailed));

  // Note: this changes the verification rules for root cert, future trust schemas as well; ideally from the outside this doesn't have an impact, but do we want to avoid this?
  // Per reset function in ConfigPolicyManager; For now we don't call reset as we still want root cert in our certCache, instead of asking for it again (when we want to verify) each time we update the trust schema
  // TODO: we keep a list of trust schemas for different application namespaces, but always update the one policyManager; this does not sound right
  // so even though we keep a dict of applications in this client, we can only use them as one.
  policyManager_->load(trustSchema, "updated-schema");
  onUpdateSuccess(trustSchema, it->second->getIsInitial());
  it->second->setIsInitial(false);
}

void
Bootstrap::onSchemaVerificationFailed
(const ptr_lib::shared_ptr<const Data>& data, string reason, OnUpdateSuccess onUpdateSuccess, OnUpdateFailed onUpdateFailed)
{
  // TODO: verification failure seems to deliver the last data packet that fails to verify, is this the expected behavior from the library?
  cout << "trust schema verification failed " << reason << endl;
  std::string appNamespace = data->getName().getPrefix(-2).toUri();
  cout << appNamespace << endl;

  std::map<std::string, ptr_lib::shared_ptr<AppTrustSchema>>::iterator it = trustSchemas_.find(appNamespace);
  if (it == trustSchemas_.end()) {
    cout << "unexpected: received trust schema for application namespace that's not being followed; malformed data name?" << endl;
    return;
  }    
  
  // Don't immediately ask for potentially the same content again if verification fails
  Interest newInterest(Name(data->getName()).getPrefix(-1));
  newInterest.setChildSelector(1);
  Exclude exclude;
  exclude.appendAny();
  exclude.appendComponent(Name::Component::fromVersion(it->second->getVersion()));
  newInterest.setExclude(exclude);

  Interest dummyInterest(Name("/local/timeout"));
  dummyInterest.setInterestLifetimeMilliseconds(4000);
  face_.expressInterest(dummyInterest, 
    bind(&Bootstrap::onDummyData, this, _1, _2),
    bind(&Bootstrap::reexpressSchemaInterest, this, newInterest, onUpdateSuccess, onUpdateFailed));

  return;
}

void 
Bootstrap::onDummyData(const ptr_lib::shared_ptr<const ndn::Interest>& interest, const ptr_lib::shared_ptr<const ndn::Data>& data)
{
  return;
}

void
Bootstrap::reexpressSchemaInterest(Interest newInterest, OnUpdateSuccess onUpdateSuccess, OnUpdateFailed onUpdateFailed)
{
  face_.expressInterest(newInterest, 
    bind(&Bootstrap::onTrustSchemaData, this, _1, _2, onUpdateSuccess, onUpdateFailed),
    bind(&Bootstrap::onTrustSchemaTimeout, this, _1, onUpdateSuccess, onUpdateFailed),
    bind(&Bootstrap::onNetworkNackSchema, this, _1, _2, onUpdateSuccess, onUpdateFailed));
}

void
Bootstrap::onNetworkNackSchema
(const ptr_lib::shared_ptr<const Interest>& interest, const ptr_lib::shared_ptr<NetworkNack>& networkNack, OnUpdateSuccess onUpdateSuccess, OnUpdateFailed onUpdateFailed)
{
  if (onUpdateFailed) {
    onUpdateFailed("Network Nack for trust schema update.");
  }
  cout << "Network NACK not yet handled" << endl;
  return;
}

/**
 * Helper functions
 */
void 
Bootstrap::onRegisterFailed
(const ptr_lib::shared_ptr<const Name>& prefix)
{
  cout << "Prefix registration failed " << prefix->toUri() << endl;
  return;
}

Name
Bootstrap::getIdentityNameFromCertName
(Name certName)
{
  int i = certName.size() - 1;

  string idString = "KEY";
  while (i >= 0) {
    if (certName.get(i).toEscapedString() == idString)
      break;
    i -= 1;
  }
      
  if (i < 0) {
    cout << "Error: unexpected certName " << certName.toUri() << endl;
    return Name();
  }

  return certName.getPrefix(i);
}

}