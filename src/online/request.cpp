//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2013 Glenn De Jonghe
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#include "online/request.hpp"

#include <curl/curl.h>
#include <assert.h>
#include <online/http_manager.hpp>
#include <utils/translation.hpp>


namespace Online{

    class HTTPManager;

    // =========================================================================================

    Request::Request(int priority, bool manage_memory)
    {
        m_priority      = priority;
        m_manage_memory = manage_memory;
        m_cancel        = false;
        m_done.setAtomic(false);
    }   // Request

    Request::~Request()
    {
    }

    void Request::execute()
    {
        beforeOperation();
        operation();
        afterOperation();
    }

    void Request::afterOperation()
    {
        m_done.setAtomic(true);
    }

    // =========================================================================================

    QuitRequest::QuitRequest()
        : Request(9999,true)
    {
    }


    // =========================================================================================

    HTTPRequest::HTTPRequest(const std::string & url)
        : Request(1,false)
    {
        m_url = url;
        m_parameters = new Parameters;
        m_progress.setAtomic(0);
        m_added = false;
    }

    HTTPRequest::~HTTPRequest()
    {
        delete m_parameters;
    }

    bool HTTPRequest::isAllowedToAdd()
    {
        if (m_url.size() > 5 && ( m_url.substr(0, 5) != "http:"))
            return false;
        return true;
    }

    void HTTPRequest::afterOperation()
    {
        callback();
        Request::afterOperation();
    }

    std::string HTTPRequest::downloadPage()
    {
        CURL * curl_session;
        curl_session = curl_easy_init();
        if(!curl_session)
        {
            Log::error("online/http_functions", "Error while initialising libCurl session");
            return "";
        }

        curl_easy_setopt(curl_session, CURLOPT_URL, m_url.c_str());
        Parameters::iterator iter;
        std::string postString = "";
        for (iter = m_parameters->begin(); iter != m_parameters->end(); ++iter)
        {
           if(iter != m_parameters->begin())
               postString.append("&");
           char * escaped = curl_easy_escape(curl_session , iter->first.c_str(), iter->first.size());
           postString.append(escaped);
           curl_free(escaped);
           postString.append("=");
           escaped = curl_easy_escape(curl_session , iter->second.c_str(), iter->second.size());
           postString.append(escaped);
           curl_free(escaped);
        }
        curl_easy_setopt(curl_session, CURLOPT_POSTFIELDS, postString.c_str());
        std::string readBuffer;
        curl_easy_setopt(curl_session, CURLOPT_WRITEFUNCTION, &HTTPRequest::WriteCallback);
        curl_easy_setopt(curl_session, CURLOPT_NOPROGRESS, 0);
        curl_easy_setopt(curl_session, CURLOPT_PROGRESSDATA, this);
        curl_easy_setopt(curl_session, CURLOPT_PROGRESSFUNCTION, &HTTPRequest::progressDownload);
        curl_easy_setopt(curl_session, CURLOPT_FILE, &readBuffer);
        // Timeout
        // Reduce the connection phase timeout (it's 300 by default).
        // Add a low speed limit to have a sort of timeout in the
        // download phase. Being under 10 B/s during a certain time will
        // probably only happen when no access to the net is available.
        // The timeout is set to 20s, it should be enough to not produce
        // false positive error.
        curl_easy_setopt(curl_session, CURLOPT_CONNECTTIMEOUT, 20);
        curl_easy_setopt(curl_session, CURLOPT_LOW_SPEED_LIMIT, 10);
        curl_easy_setopt(curl_session, CURLOPT_LOW_SPEED_TIME, 20);
        CURLcode res = curl_easy_perform(curl_session);
        if(res == CURLE_OK)
        {
            Log::info("online/http_functions", "Received : %s", readBuffer.c_str());
            setProgress(1.0f);
        }
        else
        {
            Log::error("HTTPRequest::downloadPage", "curl_easy_perform() failed: %s", curl_easy_strerror(res));
            setProgress(-1.0f);
        }
        curl_easy_cleanup(curl_session);
        return readBuffer;
    }


    size_t HTTPRequest::WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
    {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

    // ----------------------------------------------------------------------------
    /** Callback function from curl: inform about progress.
     *  \param clientp
     *  \param download_total Total size of data to download.
     *  \param download_now   How much has been downloaded so far.
     *  \param upload_total   Total amount of upload.
     *  \param upload_now     How muc has been uploaded so far.
     */
    int HTTPRequest::progressDownload(void *clientp,
                                      double download_total, double download_now,
                                      double upload_total,   double upload_now)
    {
        HTTPRequest *request = (HTTPRequest *)clientp;

        HTTPManager* http_manager = HTTPManager::get();

        // Check if we are asked to abort the download. If so, signal this
        // back to libcurl by returning a non-zero status.
        if(http_manager->getAbort() || request->isCancelled() )
        {
            // Indicates to abort the current download, which means that this
            // thread will go back to the mainloop and handle the next request.
            return 1;
        }

        float f;
        if(download_now < download_total)
        {
            f = (float)download_now / (float)download_total;
            // In case of floating point rouding errors make sure that
            // 1.0 is only reached when downloadFileInternal is finished
            if (f>=1.0f) f=0.99f;
        }
        else
        {
            // Don't set progress to 1.0f; this is done in loadFileInternal
            // after checking curls return code!
            f= download_total==0 ? 0 : 0.99f;
        }
        request->setProgress(f);
        return 0;
    }   // progressDownload



    // =========================================================================================

    XMLRequest::XMLRequest(const std::string & url)
        : HTTPRequest(url)
    {
        m_info = "";
        m_success = false;
        m_result = NULL;
    }


    void XMLRequest::operation()
    {
        m_result = file_manager->createXMLTreeFromString(downloadPage());
    }

    void XMLRequest::afterOperation()
    {
        const XMLNode * xml = this->getResult();
        bool success = false;
        irr::core::stringw info;
        std::string rec_success;
        if(xml->get("success", &rec_success))
        {
            if (rec_success =="yes")
                success = true;
            xml->get("info", &info);
        }
        else
            info = _("Unable to connect to the server. Check your internet connection or try again later.");
        m_info = info;
        m_success = success;
        HTTPRequest::afterOperation();
    }
} // namespace Online
