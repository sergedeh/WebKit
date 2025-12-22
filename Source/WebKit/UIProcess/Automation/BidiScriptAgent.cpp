/*
 * Copyright (C) 2025 Apple Inc. All rights reserved.
 * Copyright (C) 2025 Microsoft Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "BidiScriptAgent.h"

#if ENABLE(WEBDRIVER_BIDI)

#include "AutomationProtocolObjects.h"
#include "FrameTreeNodeData.h"
#include "PageLoadState.h"
#include "WebAutomationSession.h"
#include "WebAutomationSessionMacros.h"
#include "WebDriverBidiProtocolObjects.h"
#include "WebFrameMetrics.h"
#include "WebFrameProxy.h"
#include "WebPageProxy.h"
#include "WebProcessPool.h"
#include <WebCore/FrameIdentifier.h>
#include <WebCore/SecurityOrigin.h>
#include <WebCore/SecurityOriginData.h>
#include <algorithm>
#include <wtf/CallbackAggregator.h>
#include <wtf/ProcessID.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/URL.h>

namespace WebKit {

using namespace Inspector;
using BrowsingContext = Inspector::Protocol::BidiBrowsingContext::BrowsingContext;
using EvaluateResultType = Inspector::Protocol::BidiScript::EvaluateResultType;

WTF_MAKE_TZONE_ALLOCATED_IMPL(BidiScriptAgent);

BidiScriptAgent::BidiScriptAgent(WebAutomationSession& session, BackendDispatcher& backendDispatcher)
    : m_session(session)
    , m_scriptDomainDispatcher(BidiScriptBackendDispatcher::create(backendDispatcher, this))
{
}

BidiScriptAgent::~BidiScriptAgent() = default;

void BidiScriptAgent::callFunction(const String& functionDeclaration, bool awaitPromise, Ref<JSON::Object>&& target, RefPtr<JSON::Array>&& arguments, std::optional<Inspector::Protocol::BidiScript::ResultOwnership>&&, RefPtr<JSON::Object>&& optionalSerializationOptions, RefPtr<JSON::Object>&& optionalThis, std::optional<bool>&& optionalUserActivation, CommandCallbackOf<Inspector::Protocol::BidiScript::EvaluateResultType, String, RefPtr<Inspector::Protocol::BidiScript::RemoteValue>, RefPtr<Inspector::Protocol::BidiScript::ExceptionDetails>>&& callback)
{
    RefPtr session = m_session.get();
    ASYNC_FAIL_WITH_PREDEFINED_ERROR_IF(!session, InternalError);

    // FIXME: handle non-BrowsingContext obtained from `Target`.
    std::optional<BrowsingContext> browsingContext = target->getString("context"_s);
    ASYNC_FAIL_WITH_PREDEFINED_ERROR_IF(!browsingContext, InvalidParameter);

    auto pageAndFrameHandles = session->extractBrowsingContextHandles(*browsingContext);
    ASYNC_FAIL_IF_UNEXPECTED_RESULT(pageAndFrameHandles);
    auto& [topLevelContextHandle, frameHandle] = pageAndFrameHandles.value();

    // FIXME: handle `awaitPromise` option.
    // FIXME: handle `resultOwnership` option.
    // FIXME: handle `serializationOptions` option.
    // FIXME: handle custom `this` option.
    // FIXME: handle `userActivation` option.

    Ref<JSON::Array> argumentsArray = arguments ? arguments.releaseNonNull() : JSON::Array::create();

    String realmID = generateRealmIdForBrowsingContext(*browsingContext);
    session->evaluateJavaScriptFunction(topLevelContextHandle, frameHandle, functionDeclaration, WTF::move(argumentsArray), false, optionalUserActivation.value_or(false), std::nullopt, [callback = WTF::move(callback), realmID](Inspector::CommandResult<String>&& stringResult) {
        // FIXME: Properly fill ExceptionDetails remaining fields once we have a way to get them instead of just the error message.
        // https://bugs.webkit.org/show_bug.cgi?id=288058
        if (!stringResult) {
            if (stringResult.error().startsWith("JavaScriptError"_s)) {
                auto exceptionValue = Inspector::Protocol::BidiScript::RemoteValue::create()
                    .setType(Inspector::Protocol::BidiScript::RemoteValueType::Error)
                    .release();
                auto stackTrace = Inspector::Protocol::BidiScript::StackTrace::create()
                    .setCallFrames(JSON::ArrayOf<Inspector::Protocol::BidiScript::StackFrame>::create())
                    .release();
                auto exceptionDetails = Inspector::Protocol::BidiScript::ExceptionDetails::create()
                    .setText(stringResult.error().right("JavaScriptError;"_s.length()))
                    .setLineNumber(0)
                    .setColumnNumber(0)
                    .setException(WTF::move(exceptionValue))
                    .setStackTrace(WTF::move(stackTrace))
                    .release();

                callback({ { EvaluateResultType::Exception, realmID, nullptr, WTF::move(exceptionDetails) } });
                return;
            }

            callback(makeUnexpected(stringResult.error()));
            return;
        }

        auto resultValue = JSON::Value::parseJSON(stringResult.value());
        ASYNC_FAIL_WITH_PREDEFINED_ERROR_AND_DETAILS_IF(!resultValue, InternalError, "Failed to parse callFunction result as JSON"_s);

        auto resultObject = Inspector::Protocol::BidiScript::RemoteValue::create()
            .setType(Inspector::Protocol::BidiScript::RemoteValueType::Object)
            .release();

        resultObject->setValue(resultValue.releaseNonNull());

        callback({ { EvaluateResultType::Success, realmID, WTF::move(resultObject), nullptr } });
    });
}

void BidiScriptAgent::evaluate(const String& expression, bool awaitPromise, Ref<JSON::Object>&& target, std::optional<Inspector::Protocol::BidiScript::ResultOwnership>&&, RefPtr<JSON::Object>&& optionalSerializationOptions, std::optional<bool>&& optionalUserActivation, CommandCallbackOf<Inspector::Protocol::BidiScript::EvaluateResultType, String, RefPtr<Inspector::Protocol::BidiScript::RemoteValue>, RefPtr<Inspector::Protocol::BidiScript::ExceptionDetails>>&& callback)
{
    RefPtr session = m_session.get();
    ASYNC_FAIL_WITH_PREDEFINED_ERROR_IF(!session, InternalError);

    // FIXME: handle non-BrowsingContext obtained from `Target`.
    std::optional<BrowsingContext> browsingContext = target->getString("context"_s);
    ASYNC_FAIL_WITH_PREDEFINED_ERROR_IF(!browsingContext, InvalidParameter);

    auto pageAndFrameHandles = session->extractBrowsingContextHandles(*browsingContext);
    ASYNC_FAIL_IF_UNEXPECTED_RESULT(pageAndFrameHandles);
    auto& [topLevelContextHandle, frameHandle] = pageAndFrameHandles.value();

    // FIXME: handle `awaitPromise` option.
    // FIXME: handle `resultOwnership` option.
    // FIXME: handle `serializationOptions` option.

    String functionDeclaration = makeString("function() {\n return "_s, expression, "; \n}"_s);
    String realmId = generateRealmIdForBrowsingContext(*browsingContext);
    session->evaluateJavaScriptFunction(topLevelContextHandle, frameHandle, functionDeclaration, JSON::Array::create(), false, optionalUserActivation.value_or(false), std::nullopt, [callback = WTF::move(callback), realmId](Inspector::CommandResult<String>&& result) {
        auto evaluateResultType = result.has_value() ? EvaluateResultType::Success : EvaluateResultType::Exception;
        auto resultObject = Inspector::Protocol::BidiScript::RemoteValue::create()
            .setType(Inspector::Protocol::BidiScript::RemoteValueType::Object)
            .release();

        // FIXME: handle serializing different RemoteValue types as JSON here.
        if (result)
            resultObject->setValue(JSON::Value::create(WTF::move(result.value())));

        callback({ { evaluateResultType, realmId, WTF::move(resultObject), nullptr } });
    });
}

void BidiScriptAgent::getRealms(const BrowsingContext& optionalBrowsingContext, std::optional<Inspector::Protocol::BidiScript::RealmType>&& optionalRealmType, Inspector::CommandCallback<Ref<JSON::ArrayOf<Inspector::Protocol::BidiScript::RealmInfo>>>&& callback)
{
    // https://w3c.github.io/webdriver-bidi/#command-script-getRealms

    // FIXME: Implement worker realm support (dedicated-worker, shared-worker, service-worker, worker).
    // https://bugs.webkit.org/show_bug.cgi?id=304300
    // Currently only window realms (main frames and iframes) are supported.
    // Worker realm types require tracking worker global scopes and their owner sets.

    // FIXME: Implement worklet realm support (paint-worklet, audio-worklet, worklet).
    // https://bugs.webkit.org/show_bug.cgi?id=304301

    RefPtr session = m_session.get();
    ASYNC_FAIL_WITH_PREDEFINED_ERROR_IF(!session, InternalError);

    // Validate browsingContext parameter if provided and resolve its owning page.
    // Per W3C BiDi spec: the optional 'context' parameter is a browsingContext.BrowsingContext
    // that filters realms to those associated with the specified navigable (top-level or nested/iframe).
    std::optional<String> contextHandleFilter;
    RefPtr<WebPageProxy> resolvedPageForContext;
    if (!optionalBrowsingContext.isEmpty()) {
        contextHandleFilter = optionalBrowsingContext;

        // Only support page contexts in this PR - iframe support will be added later
        if (optionalBrowsingContext.startsWith("page-"_s))
            resolvedPageForContext = session->webPageProxyForHandle(optionalBrowsingContext);
        else
            ASYNC_FAIL_WITH_PREDEFINED_ERROR(FrameNotFound);

        ASYNC_FAIL_WITH_PREDEFINED_ERROR_IF(!resolvedPageForContext, WindowNotFound);
    }

    // Early short-circuit: if a non-window realm type is requested, return empty (we currently only support window realms).
    if (optionalRealmType && *optionalRealmType != Inspector::Protocol::BidiScript::RealmType::Window) {
        auto realmsArray = JSON::ArrayOf<Inspector::Protocol::BidiScript::RealmInfo>::create();
        callback(WTF::move(realmsArray));
        return;
    }

    // Collect pages to process based on context filter
    Deque<Ref<WebPageProxy>> pagesToProcess;

    if (contextHandleFilter && resolvedPageForContext)
        pagesToProcess.append(*resolvedPageForContext);
    else {
        // Enumerate all controlled pages; filtering by context happens during collection
        for (Ref process : session->protectedProcessPool()->processes()) {
            for (Ref page : process->pages()) {
                if (page->isControlledByAutomation())
                    pagesToProcess.append(page);
            }
        }
    }

    if (pagesToProcess.isEmpty()) {
        auto realmsArray = JSON::ArrayOf<Inspector::Protocol::BidiScript::RealmInfo>::create();
        callback(WTF::move(realmsArray));
        return;
    }

    // Process pages asynchronously using getAllFrameTrees
    processRealmsForPagesAsync(WTF::move(pagesToProcess), WTF::move(optionalRealmType), WTF::move(contextHandleFilter), { }, WTF::move(callback));
}


RefPtr<Inspector::Protocol::BidiScript::RealmInfo> BidiScriptAgent::createRealmInfoForFrame(const FrameInfoData& frameInfo)
{
    ASSERT(frameInfo.documentID);
    RefPtr session = m_session.get();
    if (!session)
        return nullptr;

    // Per W3C BiDi spec: "If you can't resolve the navigable (detached doc, bfcache edge),
    // return null for that settings — do not synthesize partial objects."
    auto contextHandle = contextHandleForFrame(frameInfo);
    if (!contextHandle)
        return nullptr;

    // Generate or reuse a realm id based on the frame's execution state so it changes on navigation/reload.
    String realmId = generateRealmIdForFrame(frameInfo);
    String origin = originStringFromSecurityOriginData(frameInfo.securityOrigin);

    auto realmInfo = Inspector::Protocol::BidiScript::RealmInfo::create()
        .setRealm(realmId)
        .setOrigin(origin)
        .setType(Inspector::Protocol::BidiScript::RealmType::Window)
        .release();

    // Set optional context field (required for window realms)
    realmInfo->setContext(*contextHandle);

    return realmInfo;
}

String BidiScriptAgent::generateRealmIdForFrame(const FrameInfoData& frameInfo)
{
    // Get the browsing context handle for this frame
    auto contextHandle = contextHandleForFrame(frameInfo);
    if (!contextHandle) {
        // Fallback to frame-based ID if we can't get context handle
        return makeString("realm-frame-"_s, String::number(frameInfo.frameID.toUInt64()));
    }

    // Use the shared m_realmNavigationCounters to ensure consistency with notifyRealmCreated/Destroyed
    auto counterIt = m_realmNavigationCounters.find(*contextHandle);
    if (counterIt == m_realmNavigationCounters.end()) {
        // No realm has been created yet for this context - return the base realm ID
        // Note: This should not normally happen since notifyRealmCreated should be called
        // before getRealms due to the sendWithAsyncReply barrier
        return makeString("realm-"_s, *contextHandle);
    }

    // Generate realm ID based on the current counter value
    uint64_t counter = counterIt->value;
    String realmId = counter <= 1
        ? makeString("realm-"_s, *contextHandle)
        : makeString("realm-"_s, *contextHandle, "-"_s, String::number(counter - 1));

    return realmId;
}

String BidiScriptAgent::generateRealmIdForBrowsingContext(const String& browsingContext)
{
    // For evaluate/callFunction, we need to generate consistent realm IDs based on the browsing context.
    // This simplified version works for main window contexts (page handles).
    // For now, we just use the browsing context handle as the realm ID base.
    // This will match what getRealms() generates for main frames since contextHandleForFrame returns the page handle.

    // The realm ID should match the format used by generateRealmIdForFrame()
    return makeString("realm-"_s, browsingContext);
}

String BidiScriptAgent::originStringFromSecurityOriginData(const WebCore::SecurityOriginData& originData)
{
    if (originData.isOpaque())
        return "null"_s;

    return originData.toString();
}

void BidiScriptAgent::processRealmsForPagesAsync(Deque<Ref<WebPageProxy>>&& pagesToProcess, std::optional<Inspector::Protocol::BidiScript::RealmType>&& optionalRealmType, std::optional<String>&& contextHandleFilter, Vector<RefPtr<Inspector::Protocol::BidiScript::RealmInfo>>&& accumulated, Inspector::CommandCallback<Ref<JSON::ArrayOf<Inspector::Protocol::BidiScript::RealmInfo>>>&& callback)
{
    if (pagesToProcess.isEmpty()) {
        // Assemble final array with window realms only
        auto realmsArray = JSON::ArrayOf<Inspector::Protocol::BidiScript::RealmInfo>::create();
        for (auto& realmInfo : accumulated) {
            if (!realmInfo)
                continue;
            if (optionalRealmType && *optionalRealmType != Inspector::Protocol::BidiScript::RealmType::Window)
                continue; // Only window realms supported currently.

            realmsArray->addItem(realmInfo.releaseNonNull());
        }

        callback(WTF::move(realmsArray));
        return;
    }

    // Process the first page and recursively handle the rest
    Ref<WebPageProxy> currentPage = pagesToProcess.first();
    pagesToProcess.removeFirst();

    currentPage->getAllFrameTrees([weakThis = WeakPtr { *this }, pagesToProcess = WTF::move(pagesToProcess), optionalRealmType = WTF::move(optionalRealmType), contextHandleFilter = WTF::move(contextHandleFilter), accumulated = WTF::move(accumulated), callback = WTF::move(callback)](Vector<FrameTreeNodeData>&& frameTrees) mutable {
        CheckedPtr protectedThis = weakThis.get();
        if (!protectedThis)
            return;
        // Collect realms from main frames only (no iframes in this PR)
        Vector<RefPtr<Inspector::Protocol::BidiScript::RealmInfo>> candidateRealms;
        for (const auto& frameTree : frameTrees)
            protectedThis->collectExecutionReadyFrameRealms(frameTree, candidateRealms, contextHandleFilter, false);

        for (auto& realmInfo : candidateRealms)
            accumulated.append(WTF::move(realmInfo));

        protectedThis->processRealmsForPagesAsync(WTF::move(pagesToProcess), WTF::move(optionalRealmType), WTF::move(contextHandleFilter), WTF::move(accumulated), WTF::move(callback));
    });
}

bool BidiScriptAgent::isFrameExecutionReady(const FrameInfoData& frameInfo)
{
    // Per W3C BiDi spec step 1: environment settings with execution ready flag set.
    // For enumerating realms (getRealms), we only require a committed document (documentID).
    // Remote frames (out-of-process) must still be considered: they have realms even if we
    // cannot execute scripts directly from the UI process.

    // Must have a valid document/script execution context
    if (!frameInfo.documentID)
        return false;

    // Do not exclude remote frames for enumeration purposes.

    // We intentionally do not check errorOccurred. Per spec, iframe realms may exist despite
    // loading errors, and tests expect realms to be present.

    return true;
}

std::optional<String> BidiScriptAgent::contextHandleForFrame(const FrameInfoData& frameInfo)
{
    RefPtr session = m_session.get();
    if (!session)
        return std::nullopt;

    // FIXME: Add support for iframe contexts.
    // https://bugs.webkit.org/show_bug.cgi?id=304305
    if (!frameInfo.isMainFrame)
        return std::nullopt;

    if (frameInfo.webPageProxyID) {
        for (Ref process : session->protectedProcessPool()->processes()) {
            for (Ref page : process->pages()) {
                if (page->identifier() == *frameInfo.webPageProxyID)
                    return session->handleForWebPageProxy(page);
            }
        }
    }

    return std::nullopt;
}

void BidiScriptAgent::collectExecutionReadyFrameRealms(const FrameTreeNodeData& frameTree, Vector<RefPtr<Inspector::Protocol::BidiScript::RealmInfo>>& realms, const std::optional<String>& contextHandleFilter, bool recurseSubframes)
{
    // FIXME: Per W3C BiDi spec, when contextHandleFilter is present, we should also include
    // worker realms whose owner set includes the active document of that context.
    // Currently only collecting window realms (frames).

    // Check if frame is execution ready per W3C BiDi spec step 1:
    // "Let environment settings be a list of all the environment settings objects that have their execution ready flag set."
    if (isFrameExecutionReady(frameTree.info)) {
        auto handle = contextHandleForFrame(frameTree.info);
        bool shouldInclude = !contextHandleFilter || (handle && *handle == *contextHandleFilter);
        if (shouldInclude) {
            if (auto realmInfo = createRealmInfoForFrame(frameTree.info))
                realms.append(realmInfo);
        }
    }

    // FIXME: The recurseSubframes parameter is currently always called with false since this PR
    // only supports main frame contexts. When iframe support is added, this will be used to
    // recursively collect realms from nested browsing contexts (iframes).
    // Recurse into subframes if requested
    if (recurseSubframes) {
        for (const auto& child : frameTree.children)
            collectExecutionReadyFrameRealms(child, realms, contextHandleFilter, true);
    }
}

void BidiScriptAgent::notifyRealmCreated(const String& browsingContext, const String& origin)
{
    RefPtr session = m_session.get();
    if (!session)
        return;

    // Generate a realm ID consistent with generateRealmIdForFrame() semantics:
    // first realm -> realm-{context}, subsequent -> realm-{context}-{counter}
    auto counterIt = m_realmNavigationCounters.find(browsingContext);
    String realmId;
    if (counterIt == m_realmNavigationCounters.end()) {
        realmId = makeString("realm-"_s, browsingContext);
        m_realmNavigationCounters.set(browsingContext, 1);
    } else {
        uint64_t counter = counterIt->value;
        realmId = makeString("realm-"_s, browsingContext, "-"_s, String::number(counter));
        counterIt->value = counter + 1;
    }

    // Store realm info for getRealms queries
    RealmInfo info;
    info.realmId = realmId;
    info.origin = origin;
    info.type = "window"_s;
    info.context = browsingContext;
    m_activeRealms.set(realmId, WTF::move(info));

    // FIXME: Only emit events to subscribers based on context-specific subscriptions.
    // https://bugs.webkit.org/show_bug.cgi?id=282981
    // Build script.realmCreated event per W3C BiDi spec
    auto event = JSON::Object::create();
    event->setString("method"_s, "script.realmCreated"_s);
    event->setString("type"_s, "event"_s);

    auto params = JSON::Object::create();
    params->setString("realm"_s, realmId);
    params->setString("origin"_s, origin);
    params->setString("type"_s, "window"_s);
    params->setString("context"_s, browsingContext);
    event->setObject("params"_s, WTF::move(params));

    // Send event immediately
    session->sendBidiMessage(event->toJSONString());
}

void BidiScriptAgent::notifyRealmDestroyed(const String& browsingContext)
{
    RefPtr session = m_session.get();
    if (!session)
        return;

    auto counterIt = m_realmNavigationCounters.find(browsingContext);
    if (counterIt == m_realmNavigationCounters.end())
        return;

    uint64_t counter = counterIt->value;
    String realmId = counter == 1
        ? makeString("realm-"_s, browsingContext)
        : makeString("realm-"_s, browsingContext, "-"_s, String::number(counter - 1));

    auto activeIt = m_activeRealms.find(realmId);
    if (activeIt != m_activeRealms.end())
        m_activeRealms.remove(activeIt);

    // FIXME: Only emit events to subscribers based on context-specific subscriptions.
    // https://bugs.webkit.org/show_bug.cgi?id=282981
    // Build script.realmDestroyed event per W3C BiDi spec
    auto event = JSON::Object::create();
    event->setString("method"_s, "script.realmDestroyed"_s);
    event->setString("type"_s, "event"_s);

    auto params = JSON::Object::create();
    params->setString("realm"_s, realmId);
    event->setObject("params"_s, WTF::move(params));

    session->sendBidiMessage(event->toJSONString());
}

bool BidiScriptAgent::hasRealmForContext(const String& browsingContext) const
{
    for (const auto& entry : m_activeRealms) {
        if (entry.value.context == browsingContext)
            return true;
    }
    return false;
}

} // namespace WebKit

#endif // ENABLE(WEBDRIVER_BIDI)
