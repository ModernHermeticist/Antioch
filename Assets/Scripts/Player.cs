using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class Player : MonoBehaviour 
{

	public float speed = 1.5f;
    public float jump = 50.0f;

    public bool canAttack = false;
    public float damage = 1.0f;

    private bool isJumping = false;
    private bool canJump = true;
    private Vector3 direction = new Vector3(0, 0, 0);

    public GameObject target;



    void Start () 
	{
        name = "Player";
	}
	
	// Update is called once per frame
	void Update () 
	{
        if (isJumping == true)
        {
            StartCoroutine(jumpDelayTimer());
            isJumping = false;
        }
        Vector3 velocity = new Vector3(0,0,0);
       
        if(Input.GetMouseButton(0) && canAttack)
        {
            target.GetComponent<Enemy>().hitPoints -= damage;
            if (target.GetComponent<Enemy>().hitPoints <= 0)
            {
                Destroy(target);
            }
        }
        if (Input.GetKey(KeyCode.Space))
        {
            if (canJump == true)
            {
                isJumping = true;
                canJump = false;
            }
        }

        if (Input.GetKey("w"))
        {
            velocity.y += speed * Time.deltaTime;
            direction.y = 1;
        }

        if (Input.GetKey("s"))
		{
			velocity.y -= speed * Time.deltaTime;
            direction.y = -1;
		}

		if (Input.GetKey("d"))
		{
			velocity.x += speed * Time.deltaTime;
            direction.x = 1;
		}

		if (Input.GetKey("a"))
		{
			velocity.x -= speed * Time.deltaTime;
            direction.x = -1;
		}

		if (velocity.magnitude > 0)
		{
            if (isJumping)
            {
                velocity = velocity.normalized * speed * jump;
            }
            else
            {
                velocity = velocity.normalized * speed;
            }
		}

        if (direction.magnitude > 0)
        {
            direction = direction.normalized;
        }
		transform.position += velocity * Time.deltaTime;

    }

	void OnCollisionEnter2D(Collision2D col)
	{
		Debug.Log("OnCollisionEnter2D");
	}

	void OnCollisionExit2D(Collision2D col)
	{
		Debug.Log("OnCollisionExit2D");
	}

    public IEnumerator jumpDelayTimer()
    {
        yield return new WaitForSeconds(3); // Wait for 3 seconds
        canJump = true;
    }

}
