#pragma once

#include <QObject>
#include <QString>

#include <string>

#include "mondoc/error.hpp"

namespace mondoc::ui {

inline QString llmErrorText(const mondoc::Error& error) {
    switch (error.kind()) {
        case mondoc::Error::Kind::Cancelled:
            return QObject::tr("The request was cancelled.");
        case mondoc::Error::Kind::Unreachable:
            return QObject::tr("LLM hub is unreachable. Check your API URL in Settings.");
        case mondoc::Error::Kind::RateLimited:
            return QObject::tr("LLM hub is rate-limiting requests. Try again in a moment.");
        case mondoc::Error::Kind::BadResponse: {
            const std::string msg = QString::fromStdString(error.message()).toLower().toStdString();
            if (msg.find("context") != std::string::npos ||
                msg.find("length") != std::string::npos) {
                return QObject::tr("Document is too large for AI field detection. "
                                   "Register the template manually.");
            }
            return QObject::tr("LLM hub returned an unexpected response. "
                               "Check the model name in Settings.");
        }
        default:
            return QObject::tr("LLM hub returned an unexpected response. "
                               "Check the model name in Settings.");
    }
}

}  // namespace mondoc::ui
