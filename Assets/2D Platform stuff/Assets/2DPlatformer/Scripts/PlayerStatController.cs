using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class PlayerStatController : MonoBehaviour 
{
	// Player Stats
	[SerializeField]
	private float curHp = 100.0f;
	[SerializeField]
	private float maxHp = 100.0f;
	[SerializeField]
	private float curStam = 45.0f;
	[SerializeField]
	private float maxStam = 45.0f;
	// Use this for initialization
	void Start () 
	{
		
	}
	
	// Update is called once per frame
	void Update () 
	{
		
	}

	public float getCurHp()
	{
		return curHp;
	}

	public float getMaxHp()
	{
		return maxHp;
	}

	public float getCurStam()
	{
		return curStam;
	}

	public float getMaxStam()
	{
		return maxStam;
	}
}
