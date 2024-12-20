#include "raw_client.h"

#include "raw_requests.h"
#include "rpc_parameters_serialization.h"

#include <yt/cpp/mapreduce/common/helpers.h>

#include <yt/cpp/mapreduce/http/helpers.h>
#include <yt/cpp/mapreduce/http/http.h>
#include <yt/cpp/mapreduce/http/requests.h>
#include <yt/cpp/mapreduce/http/retry_request.h>

#include <yt/cpp/mapreduce/interface/fluent.h>
#include <yt/cpp/mapreduce/interface/operation.h>
#include <yt/cpp/mapreduce/interface/tvm.h>

#include <library/cpp/yson/node/node_io.h>

namespace NYT::NDetail {

////////////////////////////////////////////////////////////////////////////////

THttpRawClient::THttpRawClient(const TClientContext& context)
    : Context_(context)
{ }

TNode THttpRawClient::Get(
    const TTransactionId& transactionId,
    const TYPath& path,
    const TGetOptions& options)
{
    TMutationId mutationId;
    THttpHeader header("GET", "get");
    header.MergeParameters(NRawClient::SerializeParamsForGet(transactionId, Context_.Config->Prefix, path, options));
    return NodeFromYsonString(RequestWithoutRetry(Context_, mutationId, header).Response);
}

TNode THttpRawClient::TryGet(
    const TTransactionId& transactionId,
    const TYPath& path,
    const TGetOptions& options)
{
    try {
        return Get(transactionId, path, options);
    } catch (const TErrorResponse& error) {
        if (!error.IsResolveError()) {
            throw;
        }
        return TNode();
    }
}

void THttpRawClient::Set(
    TMutationId& mutationId,
    const TTransactionId& transactionId,
    const TYPath& path,
    const TNode& value,
    const TSetOptions& options)
{
    THttpHeader header("PUT", "set");
    header.AddMutationId();
    header.MergeParameters(NRawClient::SerializeParamsForSet(transactionId, Context_.Config->Prefix, path, options));
    auto body = NodeToYsonString(value);
    RequestWithoutRetry(Context_, mutationId, header, body);
}

bool THttpRawClient::Exists(
    const TTransactionId& transactionId,
    const TYPath& path,
    const TExistsOptions& options)
{
    TMutationId mutationId;
    THttpHeader header("GET", "exists");
    header.MergeParameters(NRawClient::SerializeParamsForExists(transactionId, Context_.Config->Prefix, path, options));
    return ParseBoolFromResponse(RequestWithoutRetry(Context_, mutationId, header).Response);
}

void THttpRawClient::MultisetAttributes(
    TMutationId& mutationId,
    const TTransactionId& transactionId,
    const TYPath& path,
    const TNode::TMapType& value,
    const TMultisetAttributesOptions& options)
{
    THttpHeader header("PUT", "api/v4/multiset_attributes", false);
    header.AddMutationId();
    header.MergeParameters(NRawClient::SerializeParamsForMultisetAttributes(transactionId, Context_.Config->Prefix, path, options));
    auto body = NodeToYsonString(value);
    RequestWithoutRetry(Context_, mutationId, header, body);
}

TNodeId THttpRawClient::Create(
    TMutationId& mutationId,
    const TTransactionId& transactionId,
    const TYPath& path,
    const ENodeType& type,
    const TCreateOptions& options)
{
    THttpHeader header("POST", "create");
    header.AddMutationId();
    header.MergeParameters(NRawClient::SerializeParamsForCreate(transactionId, Context_.Config->Prefix, path, type, options));
    return ParseGuidFromResponse(RequestWithoutRetry(Context_, mutationId, header).Response);
}

TNodeId THttpRawClient::CopyWithoutRetries(
    const TTransactionId& transactionId,
    const TYPath& sourcePath,
    const TYPath& destinationPath,
    const TCopyOptions& options)
{
    TMutationId mutationId;
    THttpHeader header("POST", "copy");
    header.AddMutationId();
    header.MergeParameters(NRawClient::SerializeParamsForCopy(transactionId, Context_.Config->Prefix, sourcePath, destinationPath, options));
    return ParseGuidFromResponse(RequestWithoutRetry(Context_, mutationId, header).Response);
}

TNodeId THttpRawClient::CopyInsideMasterCell(
    TMutationId& mutationId,
    const TTransactionId& transactionId,
    const TYPath& sourcePath,
    const TYPath& destinationPath,
    const TCopyOptions& options)
{
    THttpHeader header("POST", "copy");
    header.AddMutationId();
    auto params = NRawClient::SerializeParamsForCopy(transactionId, Context_.Config->Prefix, sourcePath, destinationPath, options);

    // Make cross cell copying disable.
    params["enable_cross_cell_copying"] = false;
    header.MergeParameters(params);
    return ParseGuidFromResponse(RequestWithoutRetry(Context_, mutationId, header).Response);
}

TNodeId THttpRawClient::MoveWithoutRetries(
    const TTransactionId& transactionId,
    const TYPath& sourcePath,
    const TYPath& destinationPath,
    const TMoveOptions& options)
{
    TMutationId mutationId;
    THttpHeader header("POST", "move");
    header.AddMutationId();
    header.MergeParameters(NRawClient::SerializeParamsForMove(transactionId, Context_.Config->Prefix, sourcePath, destinationPath, options));
    return ParseGuidFromResponse(RequestWithoutRetry(Context_, mutationId, header).Response);
}

TNodeId THttpRawClient::MoveInsideMasterCell(
    TMutationId& mutationId,
    const TTransactionId& transactionId,
    const TYPath& sourcePath,
    const TYPath& destinationPath,
    const TMoveOptions& options)
{
    THttpHeader header("POST", "move");
    header.AddMutationId();
    auto params = NRawClient::SerializeParamsForMove(transactionId, Context_.Config->Prefix, sourcePath, destinationPath, options);

    // Make cross cell copying disable.
    params["enable_cross_cell_copying"] = false;
    header.MergeParameters(params);
    return ParseGuidFromResponse(RequestWithoutRetry(Context_, mutationId, header).Response);
}

void THttpRawClient::Remove(
    TMutationId& mutationId,
    const TTransactionId& transactionId,
    const TYPath& path,
    const TRemoveOptions& options)
{
    THttpHeader header("POST", "remove");
    header.AddMutationId();
    header.MergeParameters(NRawClient::SerializeParamsForRemove(transactionId, Context_.Config->Prefix, path, options));
    RequestWithoutRetry(Context_, mutationId, header);
}

TNode::TListType THttpRawClient::List(
    const TTransactionId& transactionId,
    const TYPath& path,
    const TListOptions& options)
{
    TMutationId mutationId;
    THttpHeader header("GET", "list");

    TYPath updatedPath = AddPathPrefix(path, Context_.Config->Prefix);
    // Translate "//" to "/"
    // Translate "//some/constom/prefix/from/config/" to "//some/constom/prefix/from/config"
    if (path.empty() && updatedPath.EndsWith('/')) {
        updatedPath.pop_back();
    }
    header.MergeParameters(NRawClient::SerializeParamsForList(transactionId, Context_.Config->Prefix, updatedPath, options));
    auto result = RequestWithoutRetry(Context_, mutationId, header);
    return NodeFromYsonString(result.Response).AsList();
}

TNodeId THttpRawClient::Link(
    TMutationId& mutationId,
    const TTransactionId& transactionId,
    const TYPath& targetPath,
    const TYPath& linkPath,
    const TLinkOptions& options)
{
    THttpHeader header("POST", "link");
    header.AddMutationId();
    header.MergeParameters(NRawClient::SerializeParamsForLink(transactionId, Context_.Config->Prefix, targetPath, linkPath, options));
    return ParseGuidFromResponse(RequestWithoutRetry(Context_, mutationId, header).Response);
}

TLockId THttpRawClient::Lock(
    TMutationId& mutationId,
    const TTransactionId& transactionId,
    const TYPath& path,
    ELockMode mode,
    const TLockOptions& options)
{
    THttpHeader header("POST", "lock");
    header.AddMutationId();
    header.MergeParameters(NRawClient::SerializeParamsForLock(transactionId, Context_.Config->Prefix, path, mode, options));
    return ParseGuidFromResponse(RequestWithoutRetry(Context_, mutationId, header).Response);
}

void THttpRawClient::Unlock(
    TMutationId& mutationId,
    const TTransactionId& transactionId,
    const TYPath& path,
    const TUnlockOptions& options)
{
    THttpHeader header("POST", "unlock");
    header.AddMutationId();
    header.MergeParameters(NRawClient::SerializeParamsForUnlock(transactionId, Context_.Config->Prefix, path, options));
    RequestWithoutRetry(Context_, mutationId, header);
}

void THttpRawClient::Concatenate(
    const TTransactionId& transactionId,
    const TVector<TRichYPath>& sourcePaths,
    const TRichYPath& destinationPath,
    const TConcatenateOptions& options)
{
    TMutationId mutationId;
    THttpHeader header("POST", "concatenate");
    header.AddMutationId();
    header.MergeParameters(NRawClient::SerializeParamsForConcatenate(transactionId, Context_.Config->Prefix, sourcePaths, destinationPath, options));
    RequestWithoutRetry(Context_, mutationId, header);
}

void THttpRawClient::PingTx(const TTransactionId& transactionId)
{
    TMutationId mutationId;
    THttpHeader header("POST", "ping_tx");
    header.MergeParameters(NRawClient::SerializeParamsForPingTx(transactionId));
    TRequestConfig requestConfig;
    requestConfig.HttpConfig = NHttpClient::THttpConfig{
        .SocketTimeout = Context_.Config->PingTimeout
    };
    RequestWithoutRetry(Context_, mutationId, header);
}

TOperationAttributes THttpRawClient::GetOperation(
    const TOperationId& operationId,
    const TGetOperationOptions& options)
{
    TMutationId mutationId;
    THttpHeader header("GET", "get_operation");
    header.MergeParameters(NRawClient::SerializeParamsForGetOperation(operationId, options));
    auto result = RequestWithoutRetry(Context_, mutationId, header);
    return NRawClient::ParseOperationAttributes(NodeFromYsonString(result.Response));
}

TOperationAttributes THttpRawClient::GetOperation(
    const TString& alias,
    const TGetOperationOptions& options)
{
    TMutationId mutationId;
    THttpHeader header("GET", "get_operation");
    header.MergeParameters(NRawClient::SerializeParamsForGetOperation(alias, options));
    auto result = RequestWithoutRetry(Context_, mutationId, header);
    return NRawClient::ParseOperationAttributes(NodeFromYsonString(result.Response));
}

void THttpRawClient::AbortOperation(
    TMutationId& mutationId,
    const TOperationId& operationId)
{
    THttpHeader header("POST", "abort_op");
    header.AddMutationId();
    header.MergeParameters(NRawClient::SerializeParamsForAbortOperation(operationId));
    RequestWithoutRetry(Context_, mutationId, header);
}

void THttpRawClient::CompleteOperation(
    TMutationId& mutationId,
    const TOperationId& operationId)
{
    THttpHeader header("POST", "complete_op");
    header.AddMutationId();
    header.MergeParameters(NRawClient::SerializeParamsForCompleteOperation(operationId));
    RequestWithoutRetry(Context_, mutationId, header);
}

void THttpRawClient::SuspendOperation(
    TMutationId& mutationId,
    const TOperationId& operationId,
    const TSuspendOperationOptions& options)
{
    THttpHeader header("POST", "suspend_op");
    header.AddMutationId();
    header.MergeParameters(NRawClient::SerializeParamsForSuspendOperation(operationId, options));
    RequestWithoutRetry(Context_, mutationId, header);
}

void THttpRawClient::ResumeOperation(
    TMutationId& mutationId,
    const TOperationId& operationId,
    const TResumeOperationOptions& options)
{
    THttpHeader header("POST", "resume_op");
    header.AddMutationId();
    header.MergeParameters(NRawClient::SerializeParamsForResumeOperation(operationId, options));
    RequestWithoutRetry(Context_, mutationId, header);
}

template <typename TKey>
static THashMap<TKey, i64> GetCounts(const TNode& countsNode)
{
    THashMap<TKey, i64> counts;
    for (const auto& entry : countsNode.AsMap()) {
        counts.emplace(FromString<TKey>(entry.first), entry.second.AsInt64());
    }
    return counts;
}

TListOperationsResult THttpRawClient::ListOperations(const TListOperationsOptions& options)
{
    TMutationId mutationId;
    THttpHeader header("GET", "list_operations");
    header.MergeParameters(NRawClient::SerializeParamsForListOperations(options));
    auto responseInfo = RequestWithoutRetry(Context_, mutationId, header);
    auto resultNode = NodeFromYsonString(responseInfo.Response);

    const auto& operationNodesList = resultNode["operations"].AsList();

    TListOperationsResult result;
    result.Operations.reserve(operationNodesList.size());
    for (const auto& operationNode : operationNodesList) {
        result.Operations.push_back(NRawClient::ParseOperationAttributes(operationNode));
    }

    if (resultNode.HasKey("pool_counts")) {
        result.PoolCounts = GetCounts<TString>(resultNode["pool_counts"]);
    }
    if (resultNode.HasKey("user_counts")) {
        result.UserCounts = GetCounts<TString>(resultNode["user_counts"]);
    }
    if (resultNode.HasKey("type_counts")) {
        result.TypeCounts = GetCounts<EOperationType>(resultNode["type_counts"]);
    }
    if (resultNode.HasKey("state_counts")) {
        result.StateCounts = GetCounts<TString>(resultNode["state_counts"]);
    }
    if (resultNode.HasKey("failed_jobs_count")) {
        result.WithFailedJobsCount = resultNode["failed_jobs_count"].AsInt64();
    }

    result.Incomplete = resultNode["incomplete"].AsBool();

    return result;
}

void THttpRawClient::UpdateOperationParameters(
    const TOperationId& operationId,
    const TUpdateOperationParametersOptions& options)
{
    TMutationId mutationId;
    THttpHeader header("POST", "update_op_parameters");
    header.MergeParameters(NRawClient::SerializeParamsForUpdateOperationParameters(operationId, options));
    RequestWithoutRetry(Context_, mutationId, header);
}

NYson::TYsonString THttpRawClient::GetJob(
    const TOperationId& operationId,
    const TJobId& jobId,
    const TGetJobOptions& options)
{
    TMutationId mutationId;
    THttpHeader header("GET", "get_job");
    header.MergeParameters(NRawClient::SerializeParamsForGetJob(operationId, jobId, options));
    auto responseInfo = RequestWithoutRetry(Context_, mutationId, header);
    return NYson::TYsonString(responseInfo.Response);
}

TListJobsResult THttpRawClient::ListJobs(
    const TOperationId& operationId,
    const TListJobsOptions& options)
{
    TMutationId mutationId;
    THttpHeader header("GET", "list_jobs");
    header.MergeParameters(NRawClient::SerializeParamsForListJobs(operationId, options));
    auto responseInfo = RequestWithoutRetry(Context_, mutationId, header);
    auto resultNode = NodeFromYsonString(responseInfo.Response);

    const auto& jobNodesList = resultNode["jobs"].AsList();

    TListJobsResult result;
    result.Jobs.reserve(jobNodesList.size());
    for (const auto& jobNode : jobNodesList) {
        result.Jobs.push_back(NRawClient::ParseJobAttributes(jobNode));
    }

    if (resultNode.HasKey("cypress_job_count") && !resultNode["cypress_job_count"].IsNull()) {
        result.CypressJobCount = resultNode["cypress_job_count"].AsInt64();
    }
    if (resultNode.HasKey("controller_agent_job_count") && !resultNode["controller_agent_job_count"].IsNull()) {
        result.ControllerAgentJobCount = resultNode["scheduler_job_count"].AsInt64();
    }
    if (resultNode.HasKey("archive_job_count") && !resultNode["archive_job_count"].IsNull()) {
        result.ArchiveJobCount = resultNode["archive_job_count"].AsInt64();
    }

    return result;
}

class TResponseReader
    : public IFileReader
{
public:
    TResponseReader(const TClientContext& context, THttpHeader header)
    {
        if (context.ServiceTicketAuth) {
            header.SetServiceTicket(context.ServiceTicketAuth->Ptr->IssueServiceTicket());
        } else {
            header.SetToken(context.Token);
        }

        if (context.ImpersonationUser) {
            header.SetImpersonationUser(*context.ImpersonationUser);
        }

        auto hostName = GetProxyForHeavyRequest(context);
        auto requestId = CreateGuidAsString();

        UpdateHeaderForProxyIfNeed(hostName, context, header);

        Response_ = context.HttpClient->Request(GetFullUrl(hostName, context, header), requestId, header);
        ResponseStream_ = Response_->GetResponseStream();
    }

private:
    size_t DoRead(void* buf, size_t len) override
    {
        return ResponseStream_->Read(buf, len);
    }

    size_t DoSkip(size_t len) override
    {
        return ResponseStream_->Skip(len);
    }

private:
    NHttpClient::IHttpResponsePtr Response_;
    IInputStream* ResponseStream_;
};

IFileReaderPtr THttpRawClient::GetJobInput(
    const TJobId& jobId,
    const TGetJobInputOptions& /*options*/)
{
    THttpHeader header("GET", "get_job_input");
    header.AddParameter("job_id", GetGuidAsString(jobId));
    return new TResponseReader(Context_, std::move(header));
}

IFileReaderPtr THttpRawClient::GetJobFailContext(
    const TOperationId& operationId,
    const TJobId& jobId,
    const TGetJobFailContextOptions& /*options*/)
{
    THttpHeader header("GET", "get_job_fail_context");
    header.AddOperationId(operationId);
    header.AddParameter("job_id", GetGuidAsString(jobId));
    return new TResponseReader(Context_, std::move(header));
}

TString THttpRawClient::GetJobStderrWithRetries(
    const TOperationId& operationId,
    const TJobId& jobId,
    const TGetJobStderrOptions& /*options*/)
{
    TMutationId mutationId;
    THttpHeader header("GET", "get_job_stderr");
    header.AddOperationId(operationId);
    header.AddParameter("job_id", GetGuidAsString(jobId));
    TRequestConfig config;
    config.IsHeavy = true;
    auto responseInfo = RequestWithoutRetry(Context_, mutationId, header, {}, config);
    return responseInfo.Response;
}

IFileReaderPtr THttpRawClient::GetJobStderr(
    const TOperationId& operationId,
    const TJobId& jobId,
    const TGetJobStderrOptions& /*options*/)
{
    THttpHeader header("GET", "get_job_stderr");
    header.AddOperationId(operationId);
    header.AddParameter("job_id", GetGuidAsString(jobId));
    return new TResponseReader(Context_, std::move(header));
}

TJobTraceEvent ParseJobTraceEvent(const TNode& node)
{
    const auto& mapNode = node.AsMap();
    TJobTraceEvent result;

    if (auto idNode = mapNode.FindPtr("operation_id")) {
        result.OperationId = GetGuid(idNode->AsString());
    }
    if (auto idNode = mapNode.FindPtr("job_id")) {
        result.JobId = GetGuid(idNode->AsString());
    }
    if (auto idNode = mapNode.FindPtr("trace_id")) {
        result.TraceId = GetGuid(idNode->AsString());
    }
    if (auto eventIndexNode = mapNode.FindPtr("event_index")) {
        result.EventIndex = eventIndexNode->AsInt64();
    }
    if (auto eventNode = mapNode.FindPtr("event")) {
        result.Event = eventNode->AsString();
    }
    if (auto eventTimeNode = mapNode.FindPtr("event_time")) {
        result.EventTime = TInstant::ParseIso8601(eventTimeNode->AsString());;
    }

    return result;
}

std::vector<TJobTraceEvent> THttpRawClient::GetJobTrace(
    const TOperationId& operationId,
    const TGetJobTraceOptions& options)
{
    TMutationId mutationId;
    THttpHeader header("GET", "get_job_trace");
    header.MergeParameters(NRawClient::SerializeParamsForGetJobTrace(operationId, options));
    auto responseInfo = RequestWithoutRetry(Context_, mutationId, header);
    auto resultNode = NodeFromYsonString(responseInfo.Response);

    const auto& traceEventNodesList = resultNode.AsList();

    std::vector<TJobTraceEvent> result;
    result.reserve(traceEventNodesList.size());
    for (const auto& traceEventNode : traceEventNodesList) {
        result.push_back(ParseJobTraceEvent(traceEventNode));
    }

    return result;
}

void THttpRawClient::MountTable(
    TMutationId& mutationId,
    const TYPath& path,
    const TMountTableOptions& options)
{
    THttpHeader header("POST", "mount_table");
    header.AddMutationId();
    header.MergeParameters(NRawClient::SerializeTabletParams(Context_.Config->Prefix, path, options));
    if (options.CellId_) {
        header.AddParameter("cell_id", GetGuidAsString(*options.CellId_));
    }
    header.AddParameter("freeze", options.Freeze_);
    RequestWithoutRetry(Context_, mutationId, header);
}

void THttpRawClient::UnmountTable(
    TMutationId& mutationId,
    const TYPath& path,
    const TUnmountTableOptions& options)
{
    THttpHeader header("POST", "unmount_table");
    header.AddMutationId();
    header.MergeParameters(NRawClient::SerializeTabletParams(Context_.Config->Prefix, path, options));
    header.AddParameter("force", options.Force_);
    RequestWithoutRetry(Context_, mutationId, header);
}

void THttpRawClient::RemountTable(
    TMutationId& mutationId,
    const TYPath& path,
    const TRemountTableOptions& options)
{
    THttpHeader header("POST", "remount_table");
    header.AddMutationId();
    header.MergeParameters(NRawClient::SerializeTabletParams(Context_.Config->Prefix, path, options));
    RequestWithoutRetry(Context_, mutationId, header);
}

void THttpRawClient::ReshardTableByPivotKeys(
    TMutationId& mutationId,
    const TYPath& path,
    const TVector<TKey>& keys,
    const TReshardTableOptions& options)
{
    THttpHeader header("POST", "reshard_table");
    header.AddMutationId();
    header.MergeParameters(NRawClient::SerializeTabletParams(Context_.Config->Prefix, path, options));
    header.AddParameter("pivot_keys", BuildYsonNodeFluently().List(keys));
    RequestWithoutRetry(Context_, mutationId, header);
}

void THttpRawClient::ReshardTableByTabletCount(
    TMutationId& mutationId,
    const TYPath& path,
    i64 tabletCount,
    const TReshardTableOptions& options)
{
    THttpHeader header("POST", "reshard_table");
    header.AddMutationId();
    header.MergeParameters(NRawClient::SerializeTabletParams(Context_.Config->Prefix, path, options));
    header.AddParameter("tablet_count", tabletCount);
    RequestWithoutRetry(Context_, mutationId, header);
}

void THttpRawClient::InsertRows(
    const TYPath& path,
    const TNode::TListType& rows,
    const TInsertRowsOptions& options)
{
    TMutationId mutationId;
    THttpHeader header("PUT", "insert_rows");
    header.SetInputFormat(TFormat::YsonBinary());
    header.MergeParameters(NRawClient::SerializeParametersForInsertRows(Context_.Config->Prefix, path, options));
    auto body = NodeListToYsonString(rows);
    TRequestConfig config;
    config.IsHeavy = true;
    RequestWithoutRetry(Context_, mutationId, header, body, config);
}

void THttpRawClient::TrimRows(
    const TYPath& path,
    i64 tabletIndex,
    i64 rowCount,
    const TTrimRowsOptions& options)
{
    TMutationId mutationId;
    THttpHeader header("POST", "trim_rows");
    header.AddParameter("trimmed_row_count", rowCount);
    header.AddParameter("tablet_index", tabletIndex);
    header.MergeParameters(NRawClient::SerializeParametersForTrimRows(Context_.Config->Prefix, path, options));
    TRequestConfig config;
    config.IsHeavy = true;
    RequestWithoutRetry(Context_, mutationId, header, /*body*/ {}, config);
}

TNode::TListType THttpRawClient::LookupRows(
    const TYPath& path,
    const TNode::TListType& keys,
    const TLookupRowsOptions& options)
{
    TMutationId mutationId;
    THttpHeader header("PUT", "lookup_rows");
    header.AddPath(AddPathPrefix(path, Context_.Config->ApiVersion));
    header.SetInputFormat(TFormat::YsonBinary());
    header.SetOutputFormat(TFormat::YsonBinary());

    header.MergeParameters(BuildYsonNodeFluently().BeginMap()
        .DoIf(options.Timeout_.Defined(), [&] (TFluentMap fluent) {
            fluent.Item("timeout").Value(static_cast<i64>(options.Timeout_->MilliSeconds()));
        })
        .Item("keep_missing_rows").Value(options.KeepMissingRows_)
        .DoIf(options.Versioned_.Defined(), [&] (TFluentMap fluent) {
            fluent.Item("versioned").Value(*options.Versioned_);
        })
        .DoIf(options.Columns_.Defined(), [&] (TFluentMap fluent) {
            fluent.Item("column_names").Value(*options.Columns_);
        })
    .EndMap());

    auto body = NodeListToYsonString(keys);
    TRequestConfig config;
    config.IsHeavy = true;
    auto responseInfo = RequestWithoutRetry(Context_, mutationId, header, body, config);
    return NodeFromYsonString(responseInfo.Response, ::NYson::EYsonType::ListFragment).AsList();
}

TNode::TListType THttpRawClient::SelectRows(
    const TString& query,
    const TSelectRowsOptions& options)
{
    TMutationId mutationId;
    THttpHeader header("GET", "select_rows");
    header.SetInputFormat(TFormat::YsonBinary());
    header.SetOutputFormat(TFormat::YsonBinary());

    header.MergeParameters(BuildYsonNodeFluently().BeginMap()
        .Item("query").Value(query)
        .DoIf(options.Timeout_.Defined(), [&] (TFluentMap fluent) {
            fluent.Item("timeout").Value(static_cast<i64>(options.Timeout_->MilliSeconds()));
        })
        .DoIf(options.InputRowLimit_.Defined(), [&] (TFluentMap fluent) {
            fluent.Item("input_row_limit").Value(*options.InputRowLimit_);
        })
        .DoIf(options.OutputRowLimit_.Defined(), [&] (TFluentMap fluent) {
            fluent.Item("output_row_limit").Value(*options.OutputRowLimit_);
        })
        .Item("range_expansion_limit").Value(options.RangeExpansionLimit_)
        .Item("fail_on_incomplete_result").Value(options.FailOnIncompleteResult_)
        .Item("verbose_logging").Value(options.VerboseLogging_)
        .Item("enable_code_cache").Value(options.EnableCodeCache_)
    .EndMap());

    TRequestConfig config;
    config.IsHeavy = true;
    auto responseInfo = RequestWithoutRetry(Context_, mutationId, header, /*body*/ {}, config);
    return NodeFromYsonString(responseInfo.Response, ::NYson::EYsonType::ListFragment).AsList();
}

ui64 THttpRawClient::GenerateTimestamp()
{
    TMutationId mutationId;
    THttpHeader header("GET", "generate_timestamp");
    TRequestConfig config;
    config.IsHeavy = true;
    auto responseInfo = RequestWithoutRetry(Context_, mutationId, header, /*body*/ {}, config);
    return NodeFromYsonString(responseInfo.Response).AsUint64();
}

TAuthorizationInfo THttpRawClient::WhoAmI()
{
    TMutationId mutationId;
    THttpHeader header("GET", "auth/whoami", /*isApi*/ false);
    auto requestResult = RequestWithoutRetry(Context_, mutationId, header);
    TAuthorizationInfo result;

    NJson::TJsonValue jsonValue;
    bool ok = NJson::ReadJsonTree(requestResult.Response, &jsonValue, /*throwOnError*/ true);
    Y_ABORT_UNLESS(ok);
    result.Login = jsonValue["login"].GetString();
    result.Realm = jsonValue["realm"].GetString();
    return result;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NDetail
