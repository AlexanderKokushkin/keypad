#ifndef WARDUINO_KEYPAD_h
#define WARDUINO_KEYPAD_h
#include <Arduino.h>

// keypad and rfid shares the same power pin
// rfid should be reinitialized after every keypad power up
// we should disable keypad while transmitting (m/b not)

namespace warduino {
    
enum class key_t : uint8_t{stop=0,none=1,dopler=2,up=3,down=4,left=5,right=6,shock=7,unknown=8,};

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

  static void init();
  static void powerOn();
  static void powerOff();
  static bool isPowered(){return powered;};
  static void disable(){enabled = false;};
  static void enable(){enabled = true;}; 
  static void poll();

  static void setOnKeyChanged(onKeyChanged_t v){onKeyChangedPtr=v;};
  static void addSpell(const key_t* SpellPtr, onSpellCasted_t SpellHandlerPtr );
  static bool removeSpell( const onSpellCasted_t HandlerToRemove);
 private:
  inline static bool powered = true; // not necessary
  inline static bool enabled = true; // shock sensor is always on 
  
  static const uint16_t SCANINTERVAL   = 100; // ms
  static const uint16_t SPELLINTERVAL  = 1000; // max ms between keys in spell
 
  inline static uint32_t actionTimestamp = millis();
  inline static uint32_t scanTimestamp = 0;

  inline static key_t lastKeypressed = key_t::none;
  inline static key_t history[historySize] = {key_t::none};
  inline static SpellBookItem spellBook[spellBookSize] = {0};
  static void pushIntoHistory( key_t newKey );
  static bool isSpellMatched(const key_t* sample );
  
  inline static onKeyChanged_t onKeyChangedPtr = nullptr; 
};               

template<class T,class R> void Keypad_T<T,R>::init(){
	analogReference(INTERNAL); // 1.1v
	pinMode(T::ttp_signal, INPUT);
	pinMode(T::ttp_pwr, OUTPUT);

	// for unknown reason there could be "80" from
	// the very beginning
	analogRead(T::ttp_signal); // dirty hack
}

template<class T,class R> void Keypad_T<T,R>::powerOn(){
  digitalWrite(T::ttp_pwr, LOW); // inverted
  if (!isPowered()){ R::init(); }
  powered = true;
  actionTimestamp = millis();
}

template<class T,class R> void Keypad_T<T,R>::powerOff(){
  if (!isPowered()){return;} // is it necessary?
  powered = false;
  digitalWrite(T::ttp_pwr, HIGH); // inverted
  R::onPoweredOff();
}


template<class T,class R> bool Keypad_T<T,R>::isSpellMatched(const key_t* sample ){
  // shiftedLeft
  uint8_t lastMeaningful = historySize-1; 
  // spell could not be larger than history, terminating symbol is always "stop"
  for (uint8_t i = 0; i < historySize-1; i++ ){
    key_t x = static_cast<key_t>( pgm_read_byte_near( &sample[i] ) ); 
    if (x == key_t::stop) {
        if (i==0) return false; // empty spell 
        lastMeaningful = i-1;  
        break;
     }
  }
  
  for (int8_t i = lastMeaningful; i >=0 ; i-- ){  // i should be signed
    key_t x = static_cast<key_t>( pgm_read_byte_near( &sample[i] ) ); 
    if (history[historySize-(lastMeaningful-i)-1] != x) return false;   
  } 
  return true;
}

template<class T,class R> void Keypad_T<T,R>::poll(){
    
 key_t justKeypressed = key_t::none;   
 uint32_t savedMillis = millis(); // careful: it's perishable!
 
 if ((savedMillis-actionTimestamp)>SPELLINTERVAL){
   pushIntoHistory(key_t::none); // spoil the spell
 }

 if ((savedMillis-scanTimestamp >= SCANINTERVAL)and(enabled)){
  scanTimestamp=savedMillis;
  
  uint16_t analogValue=analogRead( T::ttp_signal );
  switch ( analogValue ){
     case T::bttnNoneLo ... T::bttnNoneHi:     justKeypressed = key_t::none;   break;
     case T::bttnDoplerLo ... T::bttnDoplerHi: 
      justKeypressed = T::doplerConnected() ? key_t::dopler : key_t::none; 
      break;
     case T::bttnDownLo ... T::bttnDownHi:     justKeypressed = key_t::down;   break;
     case T::bttnUpLo ... T::bttnUpHi:         justKeypressed = key_t::up;     break;
     case T::bttnLeftLo ... T::bttnLeftHi:     justKeypressed = key_t::left;   break;
     case T::bttnRightLo ... T::bttnRightHi:   justKeypressed = key_t::right;  break;
     case T::bttnShockLo ... T::bttnShockHi:   justKeypressed = key_t::shock;  break;      
     default: justKeypressed = key_t::unknown;
  } // switch 
  
  if (justKeypressed != lastKeypressed) {
   lastKeypressed = justKeypressed; // m/b we can use history for it?
   actionTimestamp = savedMillis;
   if (justKeypressed != key_t::none) { // TODO : m/b we should check it first??? p.s. with unknown
    
    // we have to keep "dopler" out of history to let spells work
    if ((justKeypressed != key_t::dopler)&&(justKeypressed != key_t::unknown)) { 
     pushIntoHistory(justKeypressed);
    }
    
  if (onKeyChangedPtr!=nullptr){onKeyChangedPtr( justKeypressed , analogValue );} 
    
    for (uint8_t i=0; i<spellBookSize; i++) {
     if (spellBook[i].SpellPtr != nullptr) {
       if ( isSpellMatched(spellBook[i].SpellPtr) ) {   
         if ( spellBook[i].SpellHandlerPtr != nullptr ) {
             spellBook[i].SpellHandlerPtr();
             for (uint8_t j=0; j<historySize; j++) history[j] = key_t::none; // clear history 
         }
         break;
       }
     }  
    }

   } // justKeypressed != none
  } // justKeypressed != lastKeypressed 
 } // condition of time and "enabled"
 

}

template<class T,class R> void Keypad_T<T,R>::addSpell(const key_t* SpellPtr, onSpellCasted_t SpellHandlerPtr ){
    for (uint8_t i=0; i<spellBookSize; i++) {
      if ( spellBook[i].SpellPtr == 0) {
       spellBook[i].SpellPtr = SpellPtr;  
       spellBook[i].SpellHandlerPtr = SpellHandlerPtr;
       break;       
      }  
    }
}

template<class T,class R> bool Keypad_T<T,R>::removeSpell( const onSpellCasted_t HandlerToRemove ){
  for (uint8_t i=0; i<spellBookSize; i++) {
    if ( spellBook[i].SpellHandlerPtr == HandlerToRemove ) {
       spellBook[i].SpellPtr = 0;  
       spellBook[i].SpellHandlerPtr = 0;
       return true;       
    }  
  }
  return false;
}

template<class T,class R> void Keypad_T<T,R>::pushIntoHistory( key_t newKey ){
  // shiftLeft
  for (uint8_t i = 0; i < historySize-1; i++ ) {
   history[i] = history[i+1];
  }
  history[historySize-1] = newKey;  
}


} // namespace
#endif // WARDUINO_KEYPAD_h
 
