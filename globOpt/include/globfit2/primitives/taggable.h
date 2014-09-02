#ifndef __GF2_TAGGABLE_H__
#define __GF2_TAGGABLE_H__

#include <string>
#include <map>

namespace GF2
{
    //! \brief      Super-class for the capability of storing tags (ids) in a vector.
    //!
    //!             The types are hard-coded to speed up compilation,
    //!             and because we don't know how many types we will need later.
    class Taggable
    {
        public:
            //! \brief              Stores tag for int key. Mostly used by the enum typedefs.
            //! \param[in] key      Key to store \p value at.
            //! \param[in] value    Value to store.
            //! \return             EXIT_SUCCESS
            inline int
            setTag( int key, int value )
            {
                _tags[key] = value;
                return EXIT_SUCCESS;
            }

            //! \brief              Returns tag for int key.
            //! \param[in] key      Key to store \p value at.
            //! \return             The value stored for the key. Default -1, if entry is missing.
            inline int
            getTag( int key ) const
            {
                std::map<int,int>::const_iterator it = _tags.find( key );
                if ( it != _tags.end() )
                    return _tags.at( key );
                else
                    return -1;
            }

            //! \brief              Stores tag for string key.
            //! \param[in] key      Key to store \p value at.
            //! \param[in] value    Value to store.
            //! \return             EXIT_SUCCESS
            inline int
            setTag( std::string key, int value )
            {
                _str_tags[key] = value;
                return EXIT_SUCCESS;
            }

            //! \brief              Returns tag for string key.
            //! \param[in] key      Key to store \p value at.
            //! \return             The value stored for the key. Default -1, if entry is missing.
            inline int
            getTag( std::string key ) const
            {
                std::map<std::string,int>::const_iterator it = _str_tags.find( key );
                if ( it != _str_tags.end() )
                    return _str_tags.at( key );
                else
                    return -1;
            }

        protected:
            std::map<int,int>           _tags;      //!< \brief Stores values for int keys.
            std::map<std::string,int>   _str_tags;  //!< \brief Stores values for string keys.
    }; // ... cls Taggable
} // ... ns GF2

#endif // __GF2_TAGGABLE_H__
