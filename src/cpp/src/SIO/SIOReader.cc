#include "SIO/SIOReader.h" 


#include "SIO/LCSIO.h"
#include "SIO/SIOEventHandler.h" 
#include "SIO/SIOCollectionHandler.h"
#include "SIO/SIORunHeaderHandler.h"
#include "SIO/SIOParticleHandler.h"

#include "SIO/SIOWriter.h"

#include "EVENT/LCIO.h"

#include "SIO_streamManager.h" 
#include "SIO_recordManager.h" 
#include "SIO_blockManager.h" 
#include "SIO_stream.h" 
#include "SIO_record.h" 
#include "IMPL/LCIOExceptionHandler.h"

#include <iostream>
#include <sstream>
//#include <limits>

using namespace EVENT ;
using namespace IO ;
using namespace IOIMPL ;
using namespace IMPL ;

namespace SIO {

  // small helper class to activate the unpack mode of the
  // the SIO record for lifetime of this object (the current scope)
  class SIORecordUnpack{
  protected:
    SIORecordUnpack() ;
    SIO_record* _rec ;
  public:
    SIORecordUnpack(SIO_record* rec):_rec(rec){
      _rec->setUnpack( true ) ;
    }
    ~SIORecordUnpack(){
      _rec->setUnpack( false ) ;
    }
  };
  


  //#define DEBUG 1

  SIOReader::SIOReader() 
//     :     
//     _myFilenames(0), 
//     _currentFileIndex(0) 
  {
    _myFilenames = 0 ;
    _currentFileIndex = 0 ;

    _evtP = new LCEventIOImpl* ;
    *_evtP = 0 ;

    _runP = new LCRunHeaderIOImpl* ;
    *_runP = 0 ;


    // this is our default event 
    // collections are attached to this event when blocks are read

    // FIXME : default event no longer needed ....
    //    _defaultEvt = new LCEventIOImpl ;

#ifdef DEBUG
    SIO_streamManager::setVerbosity( SIO_ALL ) ;
    SIO_recordManager::setVerbosity( SIO_ALL ) ;
    SIO_blockManager::setVerbosity( SIO_ALL ) ;
#else
    SIO_streamManager::setVerbosity( SIO_SILENT ) ;
    SIO_recordManager::setVerbosity( SIO_SILENT ) ;
    SIO_blockManager::setVerbosity( SIO_SILENT ) ;
#endif  


    LCIOExceptionHandler::createInstance() ;


  }

  SIOReader::~SIOReader(){
    delete _evtP ;
    delete _runP ;    
  }



  void SIOReader::open(const std::vector<std::string>& filenames) 
    throw( IOException , std::exception){
    
    _myFilenames = &filenames ;
    _currentFileIndex = 0 ;
    open( (*_myFilenames)[ _currentFileIndex ]  ) ;
  }

  void SIOReader::open(const std::string& filename) throw( IOException , std::exception)  {


    std::string sioFilename ;  
    // ---- we don't require the standard file extension for reading any more
    //if( !( filename.rfind(".") filename.length() ))
    //  sioFilename = filename + LCSIO::FILE_EXTENSION ;
    //else 
    sioFilename = filename ;
    
    //    const char* stream_name = LCSIO::getValidSIOName(sioFilename) ;
    std::string stream_name = LCSIO::getValidSIOName(sioFilename) ;
    _stream = SIO_streamManager::add(  stream_name.c_str() , 64 * SIO_KBYTE ) ;

    if( _stream == 0 )
      throw IOException( std::string( "[SIOReader::open()] Bad stream name: " 
    				      + stream_name  )) ;
    //    				      + std::string(stream_name)  )) ;
    //    delete[] stream_name ;


    int status = _stream->open( sioFilename.c_str() , SIO_MODE_READ ) ; 
    
    if( status != SIO_STREAM_SUCCESS ) 
      throw IOException( std::string( "[SIOReader::open()] Can't open stream: "
				      + sioFilename ) ) ;


    if( (SIOWriter::_hdrRecord = SIO_recordManager::get( LCSIO::HEADERRECORDNAME )) == 0 )
      SIOWriter::_hdrRecord = SIO_recordManager::add( LCSIO::HEADERRECORDNAME ) ;
    
    if( (SIOWriter::_evtRecord  = SIO_recordManager::get( LCSIO::EVENTRECORDNAME )) ==0 ) 
      SIOWriter::_evtRecord = SIO_recordManager::add( LCSIO::EVENTRECORDNAME ) ;
    
    if( (SIOWriter::_runRecord  = SIO_recordManager::get( LCSIO::RUNRECORDNAME )) ==0 )
      SIOWriter::_runRecord = SIO_recordManager::add( LCSIO::RUNRECORDNAME ) ;   

    // create SIOHandlers for event and header and tell SIO about it
    //    SIO_blockManager::add( new SIOEventHandler( LCSIO::EVENTBLOCKNAME, _evtP ) ) ;
    SIO_blockManager::add( new SIOEventHandler( LCSIO::HEADERBLOCKNAME, _evtP ) ) ;
    SIO_blockManager::add( new SIORunHeaderHandler( LCSIO::RUNBLOCKNAME, _runP ) ) ;

  }


  void SIOReader::readRecord() throw (IOException , EndOfDataException , std::exception) {
    // read the next record from the stream
    if( _stream->getState()== SIO_STATE_OPEN ){
      
      unsigned int status =  _stream->read( &_dummyRecord ) ;
      if( ! (status & 1)  ){

	if( status & SIO_STREAM_EOF ){

	  // if we have a list of filenames open the next file
	  if( _myFilenames != 0  && ++_currentFileIndex < _myFilenames->size()  ){
	    close() ;

	    open( (*_myFilenames)[ _currentFileIndex  ] ) ;

	    readRecord() ;
	    return ;
	  }

	  throw EndOfDataException("EOF") ;
	}

	throw IOException( std::string(" io error on stream: ") + *_stream->getName() ) ;

      }

      // if the record was an event header, we need to set up the collection handlers
      // for the next event record.
      if( ! strcmp( _dummyRecord->getName()->c_str() , LCSIO::HEADERRECORDNAME )){
	setUpHandlers() ;
      }


    }else{

      throw IOException( std::string(" stream not open: ")+ *_stream->getName() ) ;
    }
  }
  

  LCRunHeader* SIOReader::readNextRunHeader() throw (IOException , std::exception ) {
    return readNextRunHeader( LCIO::READ_ONLY ) ;
  }

  LCRunHeader* SIOReader::readNextRunHeader(int accessMode) throw (IOException , std::exception ) {

    // set the _runRecord to unpack for this scope
    SIORecordUnpack runUnp( SIOWriter::_runRecord ) ;
    
    // this might throw the exceptions
    try{ 
      readRecord() ;
    }
    catch(EndOfDataException){
      return 0 ;
    }
    
    // set the proper acces mode before returning the event
    (*_runP)->setReadOnly(  accessMode == LCIO::READ_ONLY   ) ;
    return *_runP ;
  }
  
  void SIOReader::setUpHandlers(){

    // use event *_evtP to setup the block readers from header information ....
    const std::vector<std::string>* strVec = (*_evtP)->getCollectionNames() ;
    for( std::vector<std::string>::const_iterator name = strVec->begin() ; name != strVec->end() ; name++){
      
      // remove any old handler of the same name  
      // these handlers are static - so if we write at the same time (e.g. in a recojob)
      // we remove the hanlders needed there ....
      // this needs more thought ...
      //SIO_blockManager::remove( name->c_str()  ) ;

      const LCCollection* col = (*_evtP)->getCollection( *name ) ;

      // create a collection handler, using the default event to attach the data
      // as the real event might not exist at the time the corresponding block is read
      // (order of blocks in the SIO record is undefined) 
      // collections have to be moved from the default event to the current event 
      // after the LCEvent record has been read in total (see readRecord() )
    //      SIOCollectionHandler* ch =  new SIOCollectionHandler( *name, col->getTypeName() , &_defaultEvt   )  ;

    //SIO_blockManager::add( ch  )  ; 
      
      // check if block handler exists in manager
      SIO_block* oldCH = SIO_blockManager::get( name->c_str() ) ;
      if( oldCH != NULL) {
	// remove and then delete old collection handler
	//SIO_blockManager::remove( name->c_str()  ) ;
	// the d'tor of SIo_block calls remove ....
	delete oldCH ; 
      }

      // create collection handler for event
      SIOCollectionHandler* ch = 0 ;
      try{
	ch =  new SIOCollectionHandler( *name, col->getTypeName() , _evtP )  ;
	SIO_blockManager::add( ch  )  ; 
      }
      catch(Exception& ex){
	// unsuported type
	delete ch ;
      }
    }
  }


  LCEvent* SIOReader::readNextEvent() throw (IOException , std::exception ) {

    return readNextEvent( LCIO::READ_ONLY ) ;

  }

  LCEvent* SIOReader::readNextEvent(int accessMode) throw (IOException, std::exception ) {
    

    // first, we need to read the event header 
    // to know what collections are in the event
    { // -- scope for unpacking evt header --------
      
      SIORecordUnpack hdrUnp( SIOWriter::_hdrRecord ) ;
      
      try{ 
	readRecord() ;
      }
      catch(EndOfDataException){
	return 0 ;
      }
      
    }// -- end of scope for unpacking evt header --
    
    { // now read the event record
      SIORecordUnpack evtUnp( SIOWriter::_evtRecord ) ;
      
      try{ 
	readRecord() ;
      }
      catch(EndOfDataException){
	return 0 ;
      }
      
      // set the proper acces mode before returning the event
      (*_evtP)->setAccessMode( accessMode ) ;
      
      // restore the daughter relations from the parent relations
      SIOParticleHandler::restoreParentDaughterRelations( *_evtP ) ;
      
      return *_evtP ;      
    }
  }
  

  EVENT::LCEvent * SIOReader::readEvent(int runNumber, int evtNumber) 
    throw (IOException , std::exception) {
    
    bool runFound = false ;
    bool evtFound = false ;
    // check current run - if any
    if( *_runP != 0 ){
      if( (*_runP)->getRunNumber() == runNumber ) runFound = true ;
    }
    // skip through run headers until run found or EOF
    while (!runFound ) {
      if( readNextRunHeader() == 0 ) break ; 
      runFound = ( (*_runP)->getRunNumber() == runNumber ) ;
    }
    if( !runFound ){
//       std::stringstream message ;
//       message << " run not found: " << runNumber << std::ends ;
//       throw DataNotAvailableException( message.str()  ) ;
      return 0 ;
    }
    { // -- scope for unpacking evt header --------
      SIORecordUnpack hdrUnp( SIOWriter::_hdrRecord ) ;
      while( !evtFound ){

      try{ 
	readRecord() ;
      }
      catch(EndOfDataException){
	return 0 ;
      }

	evtFound = ( (*_evtP)->getEventNumber() == evtNumber ) ;
      }
    }// -- end of scope for unpacking evt header --
    
    if( !evtFound ) return 0 ;

    { // now read the event record
      SIORecordUnpack evtUnp( SIOWriter::SIOWriter::_evtRecord ) ;
      
      try{ 
	readRecord() ;
      }
      catch(EndOfDataException){
	return 0 ;
      }
      
      // set the proper acces mode before returning the event
      // FIXME : need update mode as well
      // (*_evtP)->setAccessMode( accessMode ) ;
      (*_evtP)->setAccessMode( LCIO::READ_ONLY ) ;
      
      // restore the daughter relations from the parent relations
      SIOParticleHandler::restoreParentDaughterRelations( *_evtP ) ;

      return *_evtP ;      
    }

  }


  void SIOReader::close() throw (IOException, std::exception ){
  
    int status  =  SIO_streamManager::remove( _stream ) ;
    
    if(! (status &1) ) //  return LCIO::ERROR ;
      throw IOException( std::string("couldn't remove stream") ) ;
    // return LCIO::SUCCESS ; 
  }




  void SIOReader::registerLCEventListener(LCEventListener * ls){ 
    _evtListeners.insert( _evtListeners.end() , ls );
  }
  void SIOReader::removeLCEventListener(LCEventListener * ls){ 
    _evtListeners.erase( _evtListeners.find( ls )  );
  }
  
  void SIOReader::registerLCRunListener(LCRunListener * ls){ 
    _runListeners.insert( _runListeners.end() , ls );
  }

  void SIOReader::removeLCRunListener(LCRunListener * ls){
    _runListeners.erase( _runListeners.find( ls ) );
 }

  void SIOReader::readStream() throw ( IO::IOException, std::exception ){

    int maxInt = INT_MAX ; // numeric_limits<int>::max() ;
    readStream( maxInt ) ;
  }
  void SIOReader::readStream(int maxRecord) throw (IOException, std::exception ){
    

    bool readUntilEOF = false ;
    if( maxRecord == INT_MAX ) readUntilEOF = true ;
    
    // here we need to read all the records on the stream
    // and then notify the listeners depending on the type ....
    
    // set all known records to unpack 
    SIORecordUnpack runUnp( SIOWriter::_runRecord ) ;
    SIORecordUnpack hdrUnp( SIOWriter::_hdrRecord ) ;
    SIORecordUnpack evtUnp( SIOWriter::_evtRecord ) ;
    
    int recordsRead = 0 ;
    while( recordsRead < maxRecord ){ 
	
      try{ 
	readRecord() ;
      }
      catch(EndOfDataException){
	
	// only throw exception if a 'finite' number of records was 
	// specified that couldn't be read from the file
	if( readUntilEOF ){  
	  return ;
	}else{
	  std::stringstream message ;
	  message << "SIOReader::readStream(int maxRecord) : EOF before " 
		  << maxRecord << " records read from file" << std::ends ;
	  throw EndOfDataException( message.str())  ;
	}
      }
      
      // notify LCRunListeners 
      if( ! strcmp( _dummyRecord->getName()->c_str() , LCSIO::RUNRECORDNAME )){
	
	recordsRead++ ;

	std::set<IO::LCRunListener*>::iterator iter = _runListeners.begin() ;
	while( iter != _runListeners.end() ){

	  (*_runP)->setReadOnly( true ) ;
	  (*iter)->processRunHeader( *_runP ) ;
	  
	  (*_runP)->setReadOnly( false ) ;
	  (*iter)->modifyRunHeader( *_runP ) ;
	  iter++ ;
	}
      }
      // notify LCEventListeners 
      if( ! strcmp( _dummyRecord->getName()->c_str() , LCSIO::EVENTRECORDNAME )){
	
	recordsRead++ ;

	std::set<IO::LCEventListener*>::iterator iter = _evtListeners.begin() ;
	while( iter != _evtListeners.end() ){

	  // restore the daughter relations from the parent relations
	  SIOParticleHandler::restoreParentDaughterRelations( *_evtP ) ;
	  
	  (*_evtP)->setAccessMode( LCIO::READ_ONLY ) ; // set the proper acces mode
	  (*iter)->processEvent( *_evtP ) ;
	  
	  (*_evtP)->setAccessMode( LCIO::UPDATE ) ;
	  (*iter)->modifyEvent( *_evtP ) ;
	  iter++ ;
	  
	}
      }
    }
  }

}; // namespace
