<script lang="ts">
	import SettingsCard from '$lib/components/SettingsCard.svelte';
	import { notifications } from '$lib/components/toasts/notifications';
	import { onMount } from 'svelte';
	import Flag from '~icons/tabler/flag';

	import type { Prova } from '$lib/types/models';
	import { getProva, salvarProva } from '$lib/services/services';

	let prova = $state<Prova>({
		nome: '',
		data: '',
		local: '',
		tipo: 'especial',
		modoVoltas: 'unica',
		criterioVoltas: 'voltas_tempo',
		totalEstagios: 5
	});

	let salvando = $state(false);
	let carregando = $state(true);

	onMount(() => {
		getProva()
			.then((data) => {
				if (data) {
					prova = {
						nome: data.nome || '',
						data: data.data || '',
						local: data.local || '',
						tipo: data.tipo || 'especial',
						modoVoltas: data.modoVoltas === 'duas' ? 'duas' : 'unica',
						criterioVoltas:
							data.criterioVoltas === 'melhor_volta' ? 'melhor_volta' : 'voltas_tempo',
						totalEstagios:
							data.totalEstagios && data.totalEstagios >= 1 && data.totalEstagios <= 10
								? data.totalEstagios
								: 5
					};
					notifications.success('Prova carregada com sucesso!', 3000);
				}
			})
			.catch((error) => {
				console.error('Erro ao carregar a prova:', error);
				notifications.error('Erro ao carregar a prova. Tente novamente.', 3000);
			})
			.finally(() => {
				carregando = false;
			});
	});

	async function salvar() {
		if (salvando) return;

		const nomeLimpo = prova.nome.trim();
		const dataLimpa = prova.data.trim();
		const localLimpo = prova.local.trim();
		const tipoSelecionado = prova.tipo || 'especial';
		const modoVoltasSelecionado = prova.modoVoltas === 'duas' ? 'duas' : 'unica';
		const criterioVoltasSelecionado =
			prova.criterioVoltas === 'melhor_volta' ? 'melhor_volta' : 'voltas_tempo';
		const totalEstagiosNum = Number(prova.totalEstagios ?? 5);

		if (!nomeLimpo || !dataLimpa || !localLimpo) {
			notifications.error('Preencha todos os campos obrigatórios (Nome, Data e Local).', 3500);
			return;
		}

		if (totalEstagiosNum < 1 || totalEstagiosNum > 10) {
			notifications.error('A quantidade de estágios deve estar entre 1 e 10.', 3500);
			return;
		}

		salvando = true;
		try {
			const payload: Prova = {
				nome: nomeLimpo,
				data: dataLimpa,
				local: localLimpo,
				tipo: tipoSelecionado,
				modoVoltas: modoVoltasSelecionado,
				criterioVoltas: criterioVoltasSelecionado,
				totalEstagios: totalEstagiosNum
			};
			const provaData = await salvarProva(payload);
			if (provaData) {
				prova = {
					nome: provaData.nome || nomeLimpo,
					data: provaData.data || dataLimpa,
					local: provaData.local || localLimpo,
					tipo: provaData.tipo || tipoSelecionado,
					modoVoltas: provaData.modoVoltas === 'duas' ? 'duas' : modoVoltasSelecionado,
					criterioVoltas:
						provaData.criterioVoltas === 'melhor_volta'
							? 'melhor_volta'
							: criterioVoltasSelecionado,
					totalEstagios: provaData.totalEstagios || totalEstagiosNum
				};
				notifications.success('Prova salva com sucesso!', 3000);
			} else {
				notifications.error('Erro ao salvar a prova. Verifique as permissões.', 3000);
			}
		} catch (error) {
			console.error('Erro ao salvar a prova:', error);
			notifications.error('Erro ao salvar a prova. Verifique os dados informados.', 3000);
		} finally {
			salvando = false;
		}
	}
</script>

<SettingsCard collapsible={false} maxWidth="max-w-3xl">
	{#snippet icon()}
		<Flag class="mr-2 inline h-6 w-6 text-primary shrink-0" />
	{/snippet}
	{#snippet title()}
		<span class="text-lg sm:text-xl font-bold">Configuração da Prova</span>
	{/snippet}

	<div class="space-y-4 sm:space-y-6 pt-1 w-full min-w-0">
		<div class="bg-base-100 p-4 sm:p-6 rounded-box border border-base-200 shadow-sm w-full">
			<label for="nome" class="block mb-1.5 text-xs sm:text-sm font-semibold text-base-content">
				Nome da Prova <span class="text-error">*</span>
			</label>
			<input
				id="nome"
				type="text"
				bind:value={prova.nome}
				disabled={carregando || salvando}
				placeholder="Ex: Enduro da Montanha 2026"
				class="input input-bordered input-sm sm:input-md w-full mb-4 text-sm"
			/>

			<div class="grid grid-cols-1 sm:grid-cols-2 gap-3 mb-4">
				<div>
					<label for="data" class="block mb-1.5 text-xs sm:text-sm font-semibold text-base-content">
						Data da Prova <span class="text-error">*</span>
					</label>
					<input
						id="data"
						type="date"
						bind:value={prova.data}
						disabled={carregando || salvando}
						class="input input-bordered input-sm sm:input-md w-full text-sm font-mono"
					/>
				</div>

				<div>
					<label for="local" class="block mb-1.5 text-xs sm:text-sm font-semibold text-base-content">
						Local de Realização <span class="text-error">*</span>
					</label>
					<input
						id="local"
						type="text"
						bind:value={prova.local}
						disabled={carregando || salvando}
						placeholder="Ex: Fazenda Boa Vista"
						class="input input-bordered input-sm sm:input-md w-full text-sm"
					/>
				</div>
			</div>

			<label for="tipo" class="block mb-1.5 text-xs sm:text-sm font-semibold text-base-content">
				Modo da Prova
			</label>
			<select
				id="tipo"
				bind:value={prova.tipo}
				disabled={carregando || salvando}
				class="select select-bordered select-sm sm:select-md w-full mb-4 text-sm"
			>
				<option value="especial">Estágios Especiais (Enduro)</option>
				<option value="laps">Voltas (Laps)</option>
			</select>

			{#if prova.tipo === 'especial'}
				<label for="totalEstagios" class="block mb-1.5 text-xs sm:text-sm font-semibold text-base-content">
					Quantidade de Estágios (E1 a E{prova.totalEstagios})
				</label>
				<input
					id="totalEstagios"
					type="number"
					min="1"
					max="10"
					bind:value={prova.totalEstagios}
					disabled={carregando || salvando}
					class="input input-bordered input-sm sm:input-md w-full mb-6 text-sm font-mono"
				/>
			{:else}
				<label for="modoVoltas" class="block mb-1.5 text-xs sm:text-sm font-semibold text-base-content">
					Configuração das Estações de Voltas (Laps)
				</label>
				<select
					id="modoVoltas"
					bind:value={prova.modoVoltas}
					disabled={carregando || salvando}
					class="select select-bordered select-sm sm:select-md w-full mb-2 text-sm"
				>
					<option value="unica">Estação Única (tempo por volta na mesma estação)</option>
					<option value="duas">Duas Estações (Largada e Chegada — cada largada/chegada é 1 volta)</option>
				</select>
				<p class="text-xs text-base-content/60 mb-4">
					{#if prova.modoVoltas === 'duas'}
						Cada passagem na Largada abre uma volta e a passagem na Chegada fecha a volta, calculando o tempo gasto.
					{:else}
						A 1ª leitura registra a Largada (L0) e cada leitura seguinte fecha a volta atual e abre a próxima automaticamente.
					{/if}
				</p>

				<label for="criterioVoltas" class="block mb-1.5 text-xs sm:text-sm font-semibold text-base-content">
					Critério Principal de Classificação (Modo Voltas)
				</label>
				<select
					id="criterioVoltas"
					bind:value={prova.criterioVoltas}
					disabled={carregando || salvando}
					class="select select-bordered select-sm sm:select-md w-full mb-6 text-sm"
				>
					<option value="voltas_tempo">Mais voltas concluídas em menor tempo total acumulado</option>
					<option value="melhor_volta">Menor tempo de volta individual (Melhor Volta / Hot Lap)</option>
				</select>
			{/if}

			<div class="pt-2 border-t border-base-200 flex justify-end">
				<button
					type="button"
					onclick={salvar}
					disabled={carregando || salvando}
					class="btn btn-primary btn-sm sm:btn-md gap-2 shadow-sm w-full sm:w-auto"
				>
					{#if salvando}
						<span class="loading loading-spinner loading-sm"></span>
						<span>Salvando...</span>
					{:else}
						<span>Salvar Prova</span>
					{/if}
				</button>
			</div>
		</div>
	</div>
</SettingsCard>
