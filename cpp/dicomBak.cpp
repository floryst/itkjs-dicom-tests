#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>
#include <unordered_set>
#include <cstdio>
#include <cerrno>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "itkCommonEnums.h"
#include "itkImageIOBase.h"
#include "itkGDCMSeriesFileNames.h"
#include "itkImageSeriesReader.h"
#include "itkGDCMImageIO.h"
#include "itkVectorImage.h"

#include "json.hpp"

using json = nlohmann::json;
using ImageType = itk::VectorImage< float, 3 >;
using ReaderType = itk::ImageFileReader< ImageType >;
using FileNamesContainer = std::vector< std::string >;
using DictionaryType = itk::MetaDataDictionary;
using SOPInstanceUID = std::string;
using ImageInfo = std::pair< SOPInstanceUID, std::string >; // (SOPInstanceUID, filename)
using DicomIO = itk::GDCMImageIO;
using MetaDataStringType = itk::MetaDataObject< std::string >;
// can only index on SOPInstanceUID, since filename isn't full path
using ImageMetaIndex = std::unordered_map< SOPInstanceUID, DictionaryType >;
using SeriesIndex = std::unordered_map< std::string, std::vector< std::string > >; // seriesUID -> []files

static int rc = 0;
static ImageMetaIndex imageMetaIndex;
static SeriesIndex seriesIndex;

void list_dir( const char *path )
{
  struct dirent *entry;
  DIR *dir = opendir( path );

  if( dir == NULL )
  {
    return;
  }
  while( (entry = readdir( dir )) != NULL )
  {
    std::cerr << entry->d_name << std::endl;
  }
  closedir( dir );
}

std::string unpackMetaAsString( const itk::MetaDataObjectBase::Pointer & metaValue )
{
    using MetaDataStringType = itk::MetaDataObject< std::string >;
    MetaDataStringType::Pointer value =
      dynamic_cast< MetaDataStringType * >( metaValue.GetPointer() );
    return value->GetMetaDataObjectValue();
}

void moveFileToSeries( const std::string fileName, const std::string seriesUID, const std::string newName )
{
  if( -1 == mkdir( seriesUID.c_str(), 0777 ) )
  {
    if( errno != EEXIST )
    {
      std::cerr << "Could not make dir: " << seriesUID << std::endl;
      return;
    }
  }

  auto dst = seriesUID + "/" + newName;
  if( 0 != std::rename( fileName.c_str(), dst.c_str() ) )
  {
    std::cerr << "Could not move file: " << fileName << ", " << std::strerror(errno) << std::endl;
    // maybe remove file?
  }
}

const std::vector< std::string > import( const FileNamesContainer & files )
{
  // make tmp dir
  std::string tmpdir( "tmp" );
  if( -1 == mkdir( tmpdir.c_str(), 0777 ) )
  {
    if( errno != EEXIST )
    {
      std::cerr << "Bad error: " << errno << std::endl;
      // TODO throw rather than return
      return std::vector< std::string >();
    }
  }

  // move file to tmp dir
  for( const auto & file : files )
  {
    auto dst = tmpdir + "/" + file;
    if( 0 != std::rename( file.c_str(), dst.c_str() ) )
    {
      std::cerr << "Failed to move file: " << file << ", " << std::strerror(errno) << std::endl;
      // TODO throw rather than return
      return std::vector< std::string >();
    }
  }

  // delete files
  for( const auto & file : files )
  {
    auto dst = tmpdir + "/" + file;
    std::remove( dst.c_str() );
  }

  list_dir("tmp");

  return std::vector< std::string >();

  typedef itk::GDCMSeriesFileNames SeriesFileNames;
  SeriesFileNames::Pointer seriesFileNames = SeriesFileNames::New();
  // files are all default dumped to cwd
  seriesFileNames->SetDirectory( tmpdir );
  seriesFileNames->SetUseSeriesDetails( true );
  seriesFileNames->SetGlobalWarningDisplay( false );
  seriesFileNames->AddSeriesRestriction( "0008|0021" );
  seriesFileNames->SetRecursive( false );

  using SeriesIdContainer = std::vector<std::string>;
  const SeriesIdContainer & seriesUIDs = seriesFileNames->GetSeriesUIDs();

  for( auto seriesUID : seriesUIDs )
  {
    FileNamesContainer fileNames = seriesFileNames->GetFileNames( seriesUID.c_str() );

    typename DicomIO::Pointer dicomIO = DicomIO::New();
    dicomIO->LoadPrivateTagsOff();
    typename ReaderType::Pointer reader = ReaderType::New();
    reader->UseStreamingOn();
    reader->SetImageIO( dicomIO );

    std::vector< std::string > seriesFileList = seriesIndex[ seriesUID ];

    for( auto filename : fileNames )
    {
      std::cerr << "FILENAME: " << filename << std::endl;

      dicomIO->SetFileName( filename );
      try
      {
        dicomIO->ReadImageInformation();
      } catch ( const itk::ExceptionObject &e )
      {
        std::cerr << e.GetDescription() << std::endl;;
        return std::vector< std::string >();
      }

      // get SOPInstanceUID of dicom object in filename
      reader->SetFileName( filename );
      try
      {
        reader->Update();
      } catch ( const itk::ExceptionObject &e )
      {
        std::cerr << e.GetDescription() << std::endl;;
        return std::vector< std::string >();
      }

      DictionaryType tags = reader->GetMetaDataDictionary();

      std::string sopInstanceUID = unpackMetaAsString( tags[ "0008|0018" ] );
      ImageMetaIndex::const_iterator found = imageMetaIndex.find( sopInstanceUID );
      if( found == imageMetaIndex.end() )
      {
        imageMetaIndex.insert( { sopInstanceUID, tags } );
        std::string newName = std::to_string( seriesFileList.size() );
        seriesFileList.push_back( newName );
        moveFileToSeries( filename, seriesUID, newName );
      }
    }
  }

  return seriesUIDs;

  // make dicom dir
  // TODO handle filename conflicts in JS?
  /*
  if( -1 == mkdir( "images", 0777 ) )
  {
    if( errno != EEXIST )
    {
      std::cerr << "Bad error: " << errno << std::endl;
      return;
    }
  }
  */

  /* move
  for( const auto & file : files )
  {
    auto dst = "images/" + file;
    if( 0 != std::rename( file.c_str(), dst.c_str() ) )
    {
      std::cerr << "Bad error: " << file << ", " << std::strerror(errno) << std::endl;
    }
  }
  */

  // list_dir("images");

  /* Loads images
  if( files.size() )
  {
    SeriesFileNames::Pointer seriesFileNames = SeriesFileNames::New();
    seriesFileNames->SetDirectory( "images" );
    seriesFileNames->SetUseSeriesDetails( true );
    seriesFileNames->SetGlobalWarningDisplay( false );
    seriesFileNames->AddSeriesRestriction( "0008|0021" );
    seriesFileNames->SetRecursive( true );

    using SeriesIdContainer = std::vector<std::string>;
    const SeriesIdContainer & seriesUIDs = seriesFileNames->GetSeriesUIDs();

    typedef itk::VectorImage< float, 3 > ImageType;
    typedef itk::ImageSeriesReader< ImageType > ReaderType;
    typename ReaderType::Pointer reader = ReaderType::New();
    reader->SetMetaDataDictionaryArrayUpdate( true );
    reader->SetImageIO( gdcmImageIO );

    json output;

    for( auto uid : seriesUIDs )
    {
      FileNamesContainer fileNames = seriesFileNames->GetFileNames( uid.c_str() );
      reader->SetFileNames( fileNames );

      json seriesImagesJson;

      try
      {
        reader->Update();
      }
      catch (itk::ExceptionObject &exc)
      {
        std::cerr << "Failed to read series " << uid << std::endl;
        std::cerr << exc << std::endl;
        return;
      }

      typedef ReaderType::DictionaryArrayRawPointer DictionaryArrayPointer;
      DictionaryArrayPointer mdicts = reader->GetMetaDataDictionaryArray();

      typedef ReaderType::DictionaryRawPointer DictionaryPointer;
      for( DictionaryPointer mdict : *mdicts )
      {
        json sub;
        std::string metaValue;
        auto keys = mdict->GetKeys();
        for( std::string key : keys )
        {
          itk::ExposeMetaData<std::string>( *mdict, key, metaValue );
          sub[key] = metaValue;
        }
        seriesImagesJson.push_back(sub);
      }

      output[uid] = seriesImagesJson;
    }

    std::cout << output.dump() << std::endl;
  }
  */
}

int main( int argc, char * argv[] )
{
  if( argc < 2 )
  {
    std::cerr << "Usage: " << argv[0] << " [import|clear|remove]" << std::endl;
    return 1;
  }

  std::string action(argv[1]);

  // need some IO so emscripten will import FS module
  // otherwise, you'll get an "FS not found" error at runtime
  // https://github.com/emscripten-core/emscripten/issues/854
  std::cerr << "Action: " << action << ", runcount: " << ++rc << ", argc: " << argc << std::endl;

  if( 0 == action.compare( "import" ) && argc > 2 )
  {
    // dicom import output.json <FILES>
    std::string outFileName = argv[2];
    std::vector< std::string > rest( argv + 3, argv + argc );
    // import( rest );
    std::vector< std::string > updatedSeriesUIDs = import( rest );
    json output( updatedSeriesUIDs );

    // json output;
    std::ofstream outfile;
    outfile.open( outFileName );
    outfile << output.dump();
    outfile.close();

    // list_dir(".");
  }

  return 0;
}
