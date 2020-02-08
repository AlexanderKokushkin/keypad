#ifndef WARDUINO_KEYPAD_h
#define WARDUINO_KEYPAD_h

#include <tiny_adxl345.h>
#include <Arduino.h>

// keypad and rfid shares the same power pin
// rfid should be reinitialized after every keypad power up
// we should disable keypad while transmitting (m/b not)

namespace warduino {
    
enum class key_t : uint8_t{stop=0,none=1,up=2,down=3,left=4,right=5,shock=6,unknown=7,};

class dummyRfid{
   public:
    static void init(){};
    static void onPoweredOff(){};
}; 
  
template<class T,class R>
class Keypad_T{
 public:   
  typedef void(*onSpellCasted_t)(); 
  typedef void(*onKeyChanged_t)( key_t key ,int value );

  static const int historySize = 20; 
  static const int spellBookSize = 10;
  
  typedef key_t Spell[historySize];
  typedef struct {
      const key_t* SpellPtr; // it's const b/c it points to arr in PROGMEM
      onSpellCasted_t SpellHandlerPtr;
  } SpellBookItem;

  static bool init();
  static void powerOn();
  static void powerOff();
  static bool isPowered(){return powered;};
  static void disable(){enabled = false;};
  static void enable(){enabled = true;}; 
  static void poll();

  static void setOnKeyChanged(onKeyChanged_t v){onKeyChangedPtr=v;};
  static void addSpell(const key_t* SpellPtr, onSpellCasted_t SpellHandlerPtr );
  static bool removeSpell( const onSpellCasted_t HandlerToRemove);
  
  static bool getAccelDoubleCheck(){return accelDoubleCheck;};
  static void setAccelDoubleCheck(bool v){accelDoubleCheck=v;};
 private:
  inline static bool powered          = true; // not necessary
  inline static bool enabled          = true; // shock sensor is always on 
  inline static bool accelDoubleCheck = true; // TODO - store it in EEPROM
  
  static const uint16_t SCAN_INTERVAL_MS = 100; // ms
  static const uint16_t SPELL_SINGLE_KEY_WINDOW_MS = 1000; // max ms between keys in spell
  static const int16_t KEY_AND_TAP_INTERVAL_MS = 10;
 
  inline static uint32_t actionTimestamp = millis();
  inline static uint32_t scanTimestamp = 0;
  inline static uint32_t tapTimestamp = 0;
  
  inline static key_t lastNotNecessarilySignificantKeypressed = key_t::none;
  inline static key_t history[historySize] = {key_t::none};
  inline static SpellBookItem spellBook[spellBookSize] = {0};
  static void pushIntoHistory( key_t newKey );
  static bool isSpellMatched(const key_t* sample );
  
  inline static onKeyChanged_t onKeyChangedPtr = nullptr; 
  static void checkForSpell();
};               

template<class T,class R> bool Keypad_T<T,R>::init(){
	analogReference(INTERNAL); // 1.1v
	pinMode(T::ttp_signal, INPUT);
	pinMode(T::ttp_pwr, OUTPUT);

	// for unknown reason there could be "80" from
	// the very beginning
	analogRead(T::ttp_signal); // dirty hack
	
	// TODO organize constants properly
    if (!Adxl345::init()){setAccelDoubleCheck(false); return false;}
    Adxl345::setRangeSettings(2);
    Adxl345::setTapDetectionOnXYZ(false,false,true);
    Adxl345::setTapThreshold(7); // 2
    Adxl345::setTapDuration(15);
    Adxl345::setINT_ENABLE(false,true,false,false,false,false,false,false);	
	return(true);
}

template<class T,class R> void Keypad_T<T,R>::powerOn(){
  digitalWrite(T::ttp_pwr, LOW); // inverted
  if (!isPowered()){ R::init(); }
  powered = true;
  actionTimestamp = millis(); // necessary ?
}

template<class T,class R> void Keypad_T<T,R>::powerOff(){
  if (!isPowered()){return;} // is it necessary?
  powered = false;
  digitalWrite(T::ttp_pwr, HIGH); // inverted
  R::onPoweredOff();
}

template<class T,class R> bool Keypad_T<T,R>::isSpellMatched(const key_t* spellSequence ){
  
  // This code is ugly
  // history is shiftedLeft ticker
  // spellSequence should be smaller than history, ended with key_t::stop
  
  // 1. detecting spellSequence useful length
  uint8_t lastMeaningful = historySize-1; 
  for (uint8_t i = 0; i < historySize-1; i++ ){
    key_t x = static_cast<key_t>( pgm_read_byte_near( &spellSequence[i] ) ); 
    if (x == key_t::stop) {
        if (i==0) return false; // empty spell 
        lastMeaningful = i-1;  
        break;
     }
  }
  
  // 2. comparing history and spellSequence starting from the end
  for (int8_t i = lastMeaningful; i >=0 ; i-- ){  // i should be signed
    key_t x = static_cast<key_t>( pgm_read_byte_near( &spellSequence[i] ) ); 
    if (history[historySize-(lastMeaningful-i)-1] != x) return false;   
  } 
  return true;
  
}

template<class T,class R> void Keypad_T<T,R>::poll(){

 if (!enabled){return;};
 if (millis()-scanTimestamp<SCAN_INTERVAL_MS){return;};
 scanTimestamp=millis();
 
 if (millis()-actionTimestamp>SPELL_SINGLE_KEY_WINDOW_MS){
   pushIntoHistory(key_t::none); // spoil the spell
 }
 
 if (accelDoubleCheck){
  if(Adxl345::getINT_SOURCE() & (1<<Adxl345::BIT_SINGLE_TAP)){
    tapTimestamp = millis();	
  } 
 }
 
 key_t justKeypressed = key_t::none;   
 uint16_t analogValue=analogRead( T::ttp_signal );  
 switch (analogValue){
  case T::bttnNoneLo  ... T::bttnNoneHi:  justKeypressed = key_t::none;  break;
  case T::bttnDownLo  ... T::bttnDownHi:  justKeypressed = key_t::down;  break;
  case T::bttnUpLo    ... T::bttnUpHi:    justKeypressed = key_t::up;    break;
  case T::bttnLeftLo  ... T::bttnLeftHi:  justKeypressed = key_t::left;  break;
  case T::bttnRightLo ... T::bttnRightHi: justKeypressed = key_t::right; break;
  case T::bttnShockLo ... T::bttnShockHi: justKeypressed = key_t::shock; break; 
  default: justKeypressed = key_t::unknown;
 }
 
 if (justKeypressed==lastNotNecessarilySignificantKeypressed){return;};
 lastNotNecessarilySignificantKeypressed = justKeypressed;	 

 actionTimestamp = millis();
 
 if (accelDoubleCheck){
  int16_t gap = tapTimestamp - actionTimestamp;
  gap = gap>0?gap:-gap;
  if (gap > KEY_AND_TAP_INTERVAL_MS){return;}
 }
 
 if (justKeypressed==key_t::none){return;}
 if (justKeypressed==key_t::unknown){return;}
    
 pushIntoHistory(justKeypressed); 
    
 if (onKeyChangedPtr!=nullptr){onKeyChangedPtr( justKeypressed , analogValue );} 
    
 checkForSpell();	
}

template<class T,class R> void Keypad_T<T,R>::addSpell(const key_t* SpellPtr, onSpellCasted_t SpellHandlerPtr ){
 for(auto& item:spellBook){
  if (item.SpellPtr==0) {
   item.SpellPtr = SpellPtr;  
   item.SpellHandlerPtr = SpellHandlerPtr;
   break;       
  }  
 }
}

template<class T,class R> bool Keypad_T<T,R>::removeSpell(const onSpellCasted_t HandlerToRemove ){
  for(auto& item:spellBook){
   if (item.SpellHandlerPtr==HandlerToRemove){
     item.SpellPtr = 0;  
     item.SpellHandlerPtr = 0;
     return true;       
    }  
  }
  return false;
}

template<class T,class R> void Keypad_T<T,R>::checkForSpell(){
 for(const auto& item:spellBook){
  if (item.SpellPtr==nullptr){continue;}
  if (!isSpellMatched(item.SpellPtr)){continue;}
	  
  if (item.SpellHandlerPtr!=nullptr ){item.SpellHandlerPtr();}
	  
  for(auto& v: history){v=key_t::none;}
  break;
 }	
}

template<class T,class R> void Keypad_T<T,R>::pushIntoHistory( key_t newKey ){
  for(uint8_t i = 0;i<historySize-1;i++){history[i]=history[i+1];}
  history[historySize-1] = newKey; // shiftLeft
}

} // namespace
#endif // WARDUINO_KEYPAD_h
 
